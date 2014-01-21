/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/isa-serial.hh"
#include "fs/fs.hh"
#include <bsd/net.hh>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <cctype>
#include "elf.hh"
#include "tls.hh"
#include "msr.hh"
#include "exceptions.hh"
#include "debug.hh"
#include "drivers/pci.hh"
#include "smp.hh"
#include "xen.hh"
#include "ioapic.hh"

#include "drivers/acpi.hh"
#include "drivers/driver.hh"
#include "drivers/virtio-net.hh"
#include "drivers/virtio-blk.hh"
#include "drivers/virtio-scsi.hh"
#include "drivers/virtio-rng.hh"
#include "drivers/xenfront-xenbus.hh"

#include <osv/sched.hh>
#include "drivers/clock.hh"
#include "drivers/clockevent.hh"
#include "drivers/console.hh"
#include "drivers/pvpanic.hh"
#include <osv/barrier.hh>
#include "arch.hh"
#include "osv/trace.hh"
#include <osv/power.hh>
#include <osv/rcu.hh>
#include "mempool.hh"
#include <bsd/porting/networking.hh>
#include "dhcp.hh"
#include <osv/version.h>
#include <osv/run.hh>
#include <osv/shutdown.hh>
#include "commands.hh"

using namespace osv;

asm(".pushsection \".debug_gdb_scripts\", \"MS\",@progbits,1 \n"
    ".byte 1 \n"
    ".asciz \"scripts/loader.py\" \n"
    ".popsection \n");

elf::Elf64_Ehdr* elf_header;
size_t elf_size;
void* elf_start;
elf::tls_data tls_data;

void setup_tls(elf::init_table inittab)
{
    tls_data = inittab.tls;
    sched::init_tls(tls_data);
    memset(tls_data.start + tls_data.filesize, 0, tls_data.size - tls_data.filesize);
    extern char tcb0[]; // defined by linker script
    memcpy(tcb0, inittab.tls.start, inittab.tls.size);
    auto p = reinterpret_cast<thread_control_block*>(tcb0 + inittab.tls.size);
    p->self = p;
    processor::wrmsr(msr::IA32_FS_BASE, reinterpret_cast<uint64_t>(p));
}

extern "C" {
    void premain();
    void vfs_init(void);
    void mount_zfs_rootfs(void);
    void ramdisk_init(void);
}


void disable_pic()
{
    // PIC not present in Xen
    XENPV_ALTERNATIVE({ outb(0xff, 0x21); outb(0xff, 0xa1); }, {});
}

void premain()
{
    disable_pic();
    auto inittab = elf::get_init(elf_header);
    setup_tls(inittab);
    for (auto init = inittab.start; init < inittab.start + inittab.count; ++init) {
        (*init)();
    }
}

int main(int ac, char **av)
{
    printf("OSv " OSV_VERSION "\n");

    smp_initial_find_current_cpu()->init_on_cpu();
    void main_cont(int ac, char** av);
    sched::init([=] { main_cont(ac, av); });
}

static bool opt_leak = false;
static bool opt_noshutdown = false;
static bool opt_log_backtrace = false;
static bool opt_mount = true;
static bool opt_vga = false;
static bool opt_verbose = false;
static std::string opt_chdir;

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
        ("trace", bpo::value<std::vector<std::string>>(), "tracepoints to enable")
        ("trace-backtrace", "log backtraces in the tracepoint log")
        ("leak", "start leak detector after boot")
        ("nomount", "don't mount the file system")
        ("noshutdown", "continue running after main() returns")
        ("vga", "use vga as a console device")
        ("verbose", "be verbose, print debug messages")
        ("env", bpo::value<std::vector<std::string>>(), "set Unix-like environment variable (putenv())")
        ("cwd", bpo::value<std::vector<std::string>>(), "set current working directory")
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
        abort();
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
    opt_vga = vars.count("vga");

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

    av += nr_options;
    ac -= nr_options;
    return std::make_tuple(ac, av);
}

// return the std::string and the commands_args poiting to them as a move
std::vector<std::vector<std::string> > prepare_commands(int ac, char** av)
{
    std::vector<std::vector<std::string> > commands;
    std::string line = std::string("");
    bool ok;

    // concatenate everything
    for (auto i = 0; i < ac; i++) {
        line += std::string(av[i]) + " ";
    }

    commands = osv::parse_command_line(line, ok);

    if (!ok) {
        abort("Failed to parse commands line\n");
    }

    return commands;
}

// Java uses this global variable (supplied by Glibc) to figure out
// aproximatively where the initial thread's stack end.
// The aproximation allow to fill the variable here instead of doing it in
// osv::run.
void *__libc_stack_end;

void run_main(std::vector<std::string> &vec)
{
    auto b = std::begin(vec)++;
    auto e = std::end(vec);
    std::string command = vec[0];
    std::vector<std::string> args(b, e);
    int ret;

    if (opt_leak) {
        debug("Enabling leak detector.\n");
        memory::tracker_enabled = true;
    }

    __libc_stack_end = __builtin_frame_address(0);
    auto lib = osv::run(command, args, &ret);
    if (lib) {
        // success
        if (ret) {
            debug("program %s returned %d\n", command.c_str(), ret);
        }
        return;
    }
    printf("run_main(): cannot execute %s. Powering off.\n", command.c_str());
    osv::poweroff();
}

void* do_main_thread(void *_commands)
{
    auto commands =
         static_cast<std::vector<std::vector<std::string> > *>(_commands);

    // initialize panic drivers
    panic::pvpanic::probe_and_setup();

    // Enumerate PCI devices
    pci::pci_device_enumeration();

    // Initialize all drivers
    hw::driver_manager* drvman = hw::driver_manager::instance();
    drvman->register_driver(virtio::blk::probe);
    drvman->register_driver(virtio::scsi::probe);
    drvman->register_driver(virtio::net::probe);
    drvman->register_driver(virtio::rng::probe);
    drvman->register_driver(xenfront::xenbus::probe);
    drvman->load_all();
    drvman->list_drivers();

    randomdev::randomdev_init();

    if (opt_mount) {
        mount_zfs_rootfs();
    }

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
        dhcp_start(true);
    }

    if (!opt_chdir.empty()) {
        debug("Chdir to: '%s'\n", opt_chdir.c_str());

        if (chdir(opt_chdir.c_str()) != 0) {
            perror("chdir");
        }
        debug("chdir done\n");
    }

    // run each payload in order
    for (auto &it : *commands) {
        run_main(it);
    }

    return nullptr;
}

namespace pthread_private {
    void init_detached_pthreads_reaper();
}

void main_cont(int ac, char** av)
{
    new elf::program();
    elf::get_program()->set_search_path({"/", "/usr/lib"});
    std::vector<std::vector<std::string> > cmds;

    std::tie(ac, av) = parse_options(ac, av);
    // multiple programs can be run -> separate their arguments
    cmds = prepare_commands(ac, av);
    ioapic::init();
    smp_launch();
    memory::enable_debug_allocator();
    acpi::init();
    console::console_init(opt_vga);
    enable_trace();
    if (opt_log_backtrace) {
        // can only do this after smp_launch, otherwise the IDT is not initialized,
        // and backtrace_safe() fails as soon as we get an exception
        tracepoint_base::log_backtraces();
    }
    sched::init_detached_threads_reaper();
    rcu_init();

    vfs_init();
    ramdisk_init();

    net_init();

    processor::sti();


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
