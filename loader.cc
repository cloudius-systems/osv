/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "fs/fs.hh"
#include <bsd/init.hh>
#include <bsd/net.hh>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <cctype>
#include <osv/elf.hh>
#include "arch-tls.hh"
#include <osv/debug.hh>

#include "smp.hh"

#ifndef AARCH64_PORT_STUB
#include "drivers/acpi.hh"
#endif /* !AARCH64_PORT_STUB */

#include <osv/sched.hh>
#include <osv/barrier.hh>
#include "arch.hh"
#include "arch-setup.hh"
#include "osv/trace.hh"
#include <osv/power.hh>
#include <osv/rcu.hh>
#include <osv/mempool.hh>
#include <bsd/porting/networking.hh>
#include <bsd/porting/shrinker.h>
#include <bsd/porting/route.h>
#include <osv/dhcp.hh>
#include <osv/version.h>
#include <osv/run.hh>
#include <osv/shutdown.hh>
#include <osv/commands.hh>
#include <osv/boot.hh>
#include <osv/sampler.hh>
#include <osv/app.hh>
#include <dirent.h>
#include <iostream>
#include <fstream>

#include "drivers/zfs.hh"
#include "drivers/random.hh"
#include "drivers/console.hh"
#include "drivers/null.hh"

#include "libc/network/__dns.hh"

using namespace osv;

asm(".pushsection \".debug_gdb_scripts\", \"MS\",@progbits,1 \n"
    ".byte 1 \n"
    ".asciz \"scripts/loader.py\" \n"
    ".popsection \n");

elf::Elf64_Ehdr* elf_header __attribute__ ((aligned(8)));

size_t elf_size;
void* elf_start;
elf::tls_data tls_data;

boot_time_chart boot_time;

void setup_tls(elf::init_table inittab)
{
    tls_data = inittab.tls;
    sched::init_tls(tls_data);
    memset(tls_data.start + tls_data.filesize, 0, tls_data.size - tls_data.filesize);

    extern char tcb0[]; // defined by linker script
    arch_setup_tls(tcb0, inittab.tls.start, inittab.tls.size);
}

extern "C" {
    void premain();
    void vfs_init(void);
    void mount_zfs_rootfs(void);
    void ramdisk_init(void);
}

void premain()
{
    arch_init_early_console();

    /* besides reporting the OSV version, this string has the function
       to check if the early console really works early enough,
       without depending on prior initialization. */
    debug_early("OSv " OSV_VERSION "\n");

    arch_init_premain();

    auto inittab = elf::get_init(elf_header);

    if (inittab.tls.start == nullptr) {
        debug_early("premain: failed to get TLS data from ELF\n");
        arch::halt_no_interrupts();
    }

    setup_tls(inittab);
    boot_time.event("TLS initialization");
    for (auto init = inittab.start; init < inittab.start + inittab.count; ++init) {
        (*init)();
    }
    boot_time.event(".init functions");
}

int main(int ac, char **av)
{
    smp_initial_find_current_cpu()->init_on_cpu();
    void main_cont(int ac, char** av);
    sched::init([=] { main_cont(ac, av); });
}

static bool opt_leak = false;
static bool opt_noshutdown = false;
static bool opt_log_backtrace = false;
static bool opt_mount = true;
static bool opt_random = true;
static bool opt_init = true;
static std::string opt_console = "all";
static bool opt_verbose = false;
static std::string opt_chdir;
static bool opt_bootchart = false;
static std::vector<std::string> opt_ip;
static std::string opt_defaultgw;
static std::string opt_nameserver;

static int sampler_frequency;
static bool opt_enable_sampler = false;

std::tuple<int, char**> parse_options(int ac, char** av)
{
    namespace bpo = boost::program_options;
    namespace bpos = boost::program_options::command_line_style;

    std::vector<const char*> args = { "osv" };

    // due to https://svn.boost.org/trac/boost/ticket/6991, we can't terminate
    // command line parsing on the executable name, so we need to look for it
    // ourselves

    auto nr_options = std::find_if(av, av + ac,
                                   [](const char* arg) { return arg[0] != '-'; }) - av;
    std::copy(av, av + nr_options, std::back_inserter(args));

    bpo::options_description desc("OSv options");
    desc.add_options()
        ("help", "show help text")
        ("sampler", bpo::value<int>(), "start stack sampling profiler")
        ("trace", bpo::value<std::vector<std::string>>(), "tracepoints to enable")
        ("trace-backtrace", "log backtraces in the tracepoint log")
        ("leak", "start leak detector after boot")
        ("nomount", "don't mount the file system")
        ("norandom", "don't initialize any random device")
        ("noshutdown", "continue running after main() returns")
        ("noinit", "don't run commands from /init")
        ("verbose", "be verbose, print debug messages")
        ("console", bpo::value<std::vector<std::string>>(), "select console driver")
        ("env", bpo::value<std::vector<std::string>>(), "set Unix-like environment variable (putenv())")
        ("cwd", bpo::value<std::vector<std::string>>(), "set current working directory")
        ("bootchart", "perform a test boot measuring a time distribution of the various operations\n")
        ("ip", bpo::value<std::vector<std::string>>(), "set static IP on NIC")
        ("defaultgw", bpo::value<std::string>(), "set default gateway address")
        ("nameserver", bpo::value<std::string>(), "set nameserver address")
    ;
    bpo::variables_map vars;
    // don't allow --foo bar (require --foo=bar) so we can find the first non-option
    // argument
    int style = bpos::unix_style & ~(bpos::long_allow_next | bpos::short_allow_next);
    try {
        bpo::store(bpo::parse_command_line(args.size(), args.data(), desc, style), vars);
    } catch(std::exception &e) {
        std::cout << e.what() << '\n';
        std::cout << desc << '\n';
        osv::poweroff();
    }
    bpo::notify(vars);

    if (vars.count("help")) {
        std::cout << desc << "\n";
    }

    if (vars.count("leak")) {
        opt_leak = true;
    }

    if (vars.count("noshutdown")) {
        opt_noshutdown = true;
    }

    if (vars.count("trace-backtrace")) {
        opt_log_backtrace = true;
    }

    if (vars.count("verbose")) {
        opt_verbose = true;
        enable_verbose();
    }

    if (vars.count("sampler")) {
        sampler_frequency = vars["sampler"].as<int>();
        opt_enable_sampler = true;
    }

    if (vars.count("bootchart")) {
        opt_bootchart = true;
    }

    if (vars.count("trace")) {
        auto tv = vars["trace"].as<std::vector<std::string>>();
        for (auto t : tv) {
            std::vector<std::string> tmp;
            boost::split(tmp, t, boost::is_any_of(" ,"), boost::token_compress_on);
            for (auto t : tmp) {
                enable_tracepoint(t);
            }
        }
    }
    opt_mount = !vars.count("nomount");
    opt_random = !vars.count("norandom");
    opt_init = !vars.count("noinit");

    if (vars.count("console")) {
        auto v = vars["console"].as<std::vector<std::string>>();
        if (v.size() > 1) {
            printf("Ignoring '--console' options after the first.");
        }
        opt_console = v.front();
        debug("console=%s\n", opt_console);
    }

    if (vars.count("env")) {
        for (auto t : vars["env"].as<std::vector<std::string>>()) {
            debug("Setting in environment: %s\n", t);
            putenv(strdup(t.c_str()));
        }
    }

    if (vars.count("cwd")) {
        auto v = vars["cwd"].as<std::vector<std::string>>();
        if (v.size() > 1) {
            printf("Ignoring '--cwd' options after the first.");
        }
        opt_chdir = v.front();
    }

    if (vars.count("ip")) {
        opt_ip = vars["ip"].as<std::vector<std::string>>();
    }

    if (vars.count("defaultgw")) {
        opt_defaultgw = vars["defaultgw"].as<std::string>();
    }

    if (vars.count("nameserver")) {
        opt_nameserver = vars["nameserver"].as<std::string>();
    }

    av += nr_options;
    ac -= nr_options;
    return std::make_tuple(ac, av);
}

// return the std::string and the commands_args poiting to them as a move
std::vector<std::vector<std::string> > prepare_commands(int ac, char** av)
{
    if (ac == 0) {
        puts("This image has an empty command line. Nothing to run.");
#ifdef AARCH64_PORT_STUB
        abort(); // a good test for the backtrace code
#endif
        osv::poweroff();
    }
    std::vector<std::vector<std::string> > commands;
    std::string line = std::string("");
    bool ok;

    // concatenate everything
    for (auto i = 0; i < ac; i++) {
        line += std::string(av[i]) + " ";
    }

    commands = osv::parse_command_line(line, ok);

    if (!ok) {
        puts("Failed to parse command line.");
        osv::poweroff();
    }

    return commands;
}

static std::string read_file(std::string fn)
{
  std::ifstream in(fn, std::ios::in | std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)),
          std::istreambuf_iterator<char>());
}

void* do_main_thread(void *_commands)
{
    auto commands =
         static_cast<std::vector<std::vector<std::string> > *>(_commands);

    if (!arch_setup_console(opt_console)) {
        abort("Unknown console:%s\n", opt_console.c_str());
    }
    arch_init_drivers();
    console::console_init();
    nulldev::nulldev_init();
    if (opt_random) {
        randomdev::randomdev_init();
    }
    boot_time.event("drivers loaded");

    if (opt_mount) {
        zfsdev::zfsdev_init();
        mount_zfs_rootfs();
        bsd_shrinker_init();
    }
    boot_time.event("ZFS mounted");

    bool has_if = false;
    osv::for_each_if([&has_if] (std::string if_name) {
        if (if_name == "lo0")
            return;

        has_if = true;
        // Start DHCP by default and wait for an IP
        if (osv::start_if(if_name, "0.0.0.0", "255.255.255.0") != 0 ||
            osv::ifup(if_name) != 0)
            debug("Could not initialize network interface.\n");
    });
    if (has_if) {
        if (opt_ip.size() == 0) {
            dhcp_start(true);
        } else {
            for (auto t : opt_ip) {
                std::vector<std::string> tmp;
                boost::split(tmp, t, boost::is_any_of(" ,"), boost::token_compress_on);
                if (tmp.size() != 3)
                    abort("incorrect parameter on --ip");

                printf("%s: %s\n",tmp[0].c_str(),tmp[1].c_str());

                if (osv::start_if(tmp[0], tmp[1], tmp[2]) != 0)
                    debug("Could not initialize network interface.\n");
            }
            if (opt_defaultgw.size() != 0) {
                osv_route_add_network("0.0.0.0",
                                      "0.0.0.0",
                                      opt_defaultgw.c_str());
            }
            if (opt_nameserver.size() != 0) {
                auto addr = boost::asio::ip::address_v4::from_string(opt_nameserver);
                osv::set_dns_config({addr}, std::vector<std::string>());
            }
        }
    }

    if (!opt_chdir.empty()) {
        debug("Chdir to: '%s'\n", opt_chdir.c_str());

        if (chdir(opt_chdir.c_str()) != 0) {
            perror("chdir");
        }
        debug("chdir done\n");
    }

    if (opt_leak) {
        debug("Enabling leak detector.\n");
        memory::tracker_enabled = true;
    }

    boot_time.event("Total time");

    if (opt_bootchart) {
        boot_time.print_chart();
    }

    // Run command lines in /init/* before the manual command line
    if (opt_init) {
        std::vector<std::vector<std::string>> init_commands;
        struct dirent **namelist;
        int count = scandir("/init", &namelist, NULL, alphasort);
        for (int i = 0; i < count; i++) {
            if (!strcmp(".", namelist[i]->d_name) ||
                    !strcmp("..", namelist[i]->d_name)) {
                continue;
            }
            std::string fn("/init/");
            fn += namelist[i]->d_name;
            auto cmdline = read_file(fn);
            debug("Running from %s: %s\n", fn.c_str(), cmdline.c_str());
            bool ok;
            auto new_commands = osv::parse_command_line(cmdline, ok);
            free(namelist[i]);
            if (ok) {
                init_commands.insert(init_commands.end(),
                        new_commands.begin(), new_commands.end());
            }
        }
        free(namelist);
        commands->insert(commands->begin(),
                 init_commands.begin(), init_commands.end());
    }

    // run each payload in order
    // Our parse_command_line() leaves at the end of each command a delimiter,
    // can be '&' if we need to run this command in a new thread, or ';' or
    // empty otherwise, to run in this thread. '&!' is the same as '&', but
    // doesn't wait for the thread to finish before exiting OSv.
    std::vector<shared_app_t> bg;
    std::vector<shared_app_t> detached;
    for (auto &it : *commands) {
        std::vector<std::string> newvec(it.begin(), std::prev(it.end()));
        auto suffix = it.back();
        try {
            auto app = application::run(newvec);
            if (suffix == "&") {
                bg.push_back(app);
            } else if (suffix == "&!") {
                detached.push_back(app);
            } else {
                app->join();
            }
        } catch (const launch_error& e) {
            std::cerr << e.what() << ". Powering off.\n";
            osv::poweroff();
        }
    }

    for (auto app : bg) {
        app->join();
    }

    for (auto app : detached) {
        app->request_termination();
        debug("Requested termination of %s, waiting...\n", app->get_command());
        app->join();
        debug("Application terminated: %s\n", app->get_command());
    }

    return nullptr;
}

void main_cont(int ac, char** av)
{
    new elf::program();
    elf::get_program()->set_search_path({"/", "/usr/lib"});
    std::vector<std::vector<std::string> > cmds;

    std::tie(ac, av) = parse_options(ac, av);

    smp_launch();
    boot_time.event("SMP launched");
    memory::enable_debug_allocator();

#ifndef AARCH64_PORT_STUB
    acpi::init();
#endif /* !AARCH64_PORT_STUB */

    if (sched::cpus.size() > sched::max_cpus) {
        printf("Too many cpus, can't boot with greater than %u cpus.\n", sched::max_cpus);
        poweroff();
    }

    enable_trace();
    if (opt_log_backtrace) {
        // can only do this after smp_launch, otherwise the IDT is not initialized,
        // and backtrace_safe() fails as soon as we get an exception
        enable_backtraces();
    }
    sched::init_detached_threads_reaper();

    bsd_init();

    vfs_init();
    boot_time.event("VFS initialized");
    ramdisk_init();

    net_init();
    boot_time.event("Network initialized");

    arch::irq_enable();

#ifndef AARCH64_PORT_STUB
    if (opt_enable_sampler) {
        prof::config config{std::chrono::nanoseconds(1000000000 / sampler_frequency)};
        prof::start_sampler(config);
    }
#endif /* !AARCH64_PORT_STUB */

    // multiple programs can be run -> separate their arguments
    cmds = prepare_commands(ac, av);

    pthread_t pthread;
    // run the payload in a pthread, so pthread_self() etc. work
    pthread_create(&pthread, nullptr, do_main_thread, (void *) &cmds);
    void* retval;
    pthread_join(pthread, &retval);

    if (opt_noshutdown) {
        // If the --noshutdown option is given, continue running the system,
        // and whatever threads might be running, even after main returns
        debug("main() returned.\n");
        sched::thread::wait_until([] { return false; });
    }

    if (memory::tracker_enabled) {
        debug("Leak testing done. Please use 'osv leak show' in gdb to analyze results.\n");
        osv::halt();
    } else {
        osv::shutdown();
    }
}

int __argc;
char** __argv;
