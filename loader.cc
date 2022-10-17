/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/drivers_config.h>
#include <bsd/porting/netport.h>
#include "fs/fs.hh"
#include <bsd/init.hh>
#include <bsd/net.hh>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <cctype>
#include <osv/elf.hh>
#include "arch-tls.hh"
#include <osv/debug.hh>
#include <osv/clock.hh>
#include <osv/version.hh>

#include "smp.hh"

#ifdef __x86_64__
#if CONF_drivers_acpi
#include "drivers/acpi.hh"
#endif
#endif /* __x86_64__ */

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
#include <osv/firmware.hh>
#if CONF_drivers_xen
#include <osv/xen.hh>
#endif
#include <osv/options.hh>
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <mntent.h>

#include "drivers/zfs.hh"
#include "drivers/random.hh"
#include "drivers/console.hh"
#include "drivers/null.hh"

#include "libc/network/__dns.hh"
#include <processor.hh>
#include <dlfcn.h>

using namespace osv;
using namespace osv::clock::literals;

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

    extern char tcb0[]; // defined by linker script
    arch_setup_tls(tcb0, tls_data);
}

extern "C" {
    void premain();
    void vfs_init(void);
    void pivot_rootfs(const char*);
    void unmount_devfs();
    int mount_zfs_rootfs(bool, bool);
    int mount_rofs_rootfs(bool);
    void rofs_disable_cache();
    int mount_virtiofs_rootfs(bool);
}

void premain()
{
    arch_init_early_console();

    /* besides reporting the OSV version, this string has the function
       to check if the early console really works early enough,
       without depending on prior initialization. */
    debug_early("OSv " OSV_VERSION "\n");

    arch_init_premain();

#ifdef __x86_64__
    auto elf_header_virt_address = (void*)elf_header + OSV_KERNEL_VM_SHIFT;
#endif

#ifdef __aarch64__
    extern u64 kernel_vm_shift;
    auto elf_header_virt_address = (void*)elf_header + kernel_vm_shift;
#endif

    auto inittab = elf::get_init(reinterpret_cast<elf::Elf64_Ehdr*>(
        elf_header_virt_address));

    if (inittab.tls.start == nullptr) {
        debug_early("premain: failed to get TLS data from ELF\n");
        arch::halt_no_interrupts();
    }

    setup_tls(inittab);
    boot_time.event(3,"TLS initialization");
    for (auto init = inittab.start; init < inittab.start + inittab.count; ++init) {
        (*init)();
    }
    boot_time.event(".init functions");
}

int main(int loader_argc, char **loader_argv)
{
    smp_initial_find_current_cpu()->init_on_cpu();
    void main_cont(int loader_argc, char** loader_argv);
    sched::init([=] { main_cont(loader_argc, loader_argv); });
}

static bool opt_preload_zfs_library = false;
static bool opt_extra_zfs_pools = false;
static bool opt_disable_rofs_cache = false;
static bool opt_leak = false;
static bool opt_noshutdown = false;
bool opt_power_off_on_abort = false;
static bool opt_log_backtrace = false;
static bool opt_mount = true;
static bool opt_pivot = true;
static std::string opt_rootfs;
static bool opt_random = true;
static bool opt_init = true;
static std::string opt_console = "all";
static bool opt_verbose = false;
static std::string opt_chdir;
static bool opt_bootchart = false;
static std::vector<std::string> opt_ip;
static std::vector<std::string> opt_defaultgw;
static std::vector<std::string> opt_nameserver;
static std::string opt_redirect;
static std::chrono::nanoseconds boot_delay;
std::vector<mntent> opt_mount_fs;
bool opt_maxnic = false;
int maxnic;
bool opt_pci_disabled = false;

static int sampler_frequency;
static bool opt_enable_sampler = false;

static void usage()
{
    std::cout << "OSv options:\n";
    std::cout << "  --help                show help text\n";
    std::cout << "  --sampler=arg         start stack sampling profiler\n";
    std::cout << "  --trace=arg           tracepoints to enable\n";
    std::cout << "  --trace-backtrace     log backtraces in the tracepoint log\n";
    std::cout << "  --leak                start leak detector after boot\n";
    std::cout << "  --nomount             don't mount the root file system\n";
    std::cout << "  --nopivot             do not pivot the root from bootfs to the root fs\n";
    std::cout << "  --rootfs=arg          root filesystem to use (zfs, rofs, ramfs or virtiofs)\n";
    std::cout << "  --assign-net          assign virtio network to the application\n";
    std::cout << "  --maxnic=arg          maximum NIC number\n";
    std::cout << "  --norandom            don't initialize any random device\n";
    std::cout << "  --noshutdown          continue running after main() returns\n";
    std::cout << "  --power-off-on-abort  use poweroff instead of halt if it's aborted\n";
    std::cout << "  --noinit              don't run commands from /init\n";
    std::cout << "  --verbose             be verbose, print debug messages\n";
    std::cout << "  --console=arg         select console driver\n";
    std::cout << "  --env=arg             set Unix-like environment variable (putenv())\n";
    std::cout << "  --cwd=arg             set current working directory\n";
    std::cout << "  --bootchart           perform a test boot measuring a time distribution of\n";
    std::cout << "                        the various operations\n\n";
    std::cout << "  --ip=arg              set static IP on NIC\n";
    std::cout << "  --defaultgw=arg       set default gateway address\n";
    std::cout << "  --nameserver=arg      set nameserver address\n";
    std::cout << "  --delay=arg (=0)      delay in seconds before boot\n";
    std::cout << "  --redirect=arg        redirect stdout and stderr to file\n";
    std::cout << "  --disable_rofs_cache  disable ROFS memory cache\n";
    std::cout << "  --nopci               disable PCI enumeration\n";
    std::cout << "  --extra-zfs-pools     import extra ZFS pools\n";
    std::cout << "  --mount-fs=arg        mount extra filesystem, format:<fs_type,url,path>\n";
    std::cout << "  --preload-zfs-library preload ZFS library from /usr/lib/fs\n\n";
}

static void handle_parse_error(const std::string &message)
{
    std::cout << message << std::endl;
    usage();
    osv::poweroff();
}

static bool extract_option_flag(std::map<std::string,std::vector<std::string>> &options_values, const std::string &name)
{
    return options::extract_option_flag(options_values, name, handle_parse_error);
}

static void parse_options(int loader_argc, char** loader_argv)
{
    auto options_values = options::parse_options_values(loader_argc, loader_argv, handle_parse_error, false);

    if (extract_option_flag(options_values, "help")) {
        usage();
    }

    if (extract_option_flag(options_values, "leak")) {
        opt_leak = true;
    }

    if (extract_option_flag(options_values, "disable_rofs_cache")) {
        opt_disable_rofs_cache = true;
    }

    if (extract_option_flag(options_values, "preload-zfs-library")) {
        opt_preload_zfs_library = true;
    }

    if (extract_option_flag(options_values, "extra-zfs-pools")) {
        opt_extra_zfs_pools = true;
    }

    if (extract_option_flag(options_values, "noshutdown")) {
        opt_noshutdown = true;
    }

    if (extract_option_flag(options_values, "power-off-on-abort")) {
        opt_power_off_on_abort = true;
    }

    if (options::option_value_exists(options_values, "maxnic")) {
        opt_maxnic = true;
        maxnic = options::extract_option_int_value(options_values, "maxnic", handle_parse_error);
    }

    if (extract_option_flag(options_values, "trace-backtrace")) {
        opt_log_backtrace = true;
    }

    if (extract_option_flag(options_values, "verbose")) {
        opt_verbose = true;
        enable_verbose();
    }

    if (options::option_value_exists(options_values, "sampler")) {
        sampler_frequency = options::extract_option_int_value(options_values, "sampler", handle_parse_error);
        opt_enable_sampler = true;
    }

    if (extract_option_flag(options_values, "bootchart")) {
        opt_bootchart = true;
    }

    if (options::option_value_exists(options_values, "trace")) {
        auto tv = options::extract_option_values(options_values, "trace");
        for (auto t : tv) {
            std::vector<std::string> tmp;
            boost::split(tmp, t, boost::is_any_of(" ,"), boost::token_compress_on);
            for (auto t : tmp) {
                enable_tracepoint(t);
            }
        }
    }

    opt_mount = !extract_option_flag(options_values, "nomount");
    opt_pivot = !extract_option_flag(options_values, "nopivot");
    opt_random = !extract_option_flag(options_values, "norandom");
    opt_init = !extract_option_flag(options_values, "noinit");

    if (options::option_value_exists(options_values, "console")) {
        auto v = options::extract_option_values(options_values, "console");
        if (v.size() > 1) {
            printf("Ignoring '--console' options after the first.");
        }
        opt_console = v.front();
        debug("console=%s\n", opt_console);
    }

    if (options::option_value_exists(options_values, "rootfs")) {
        auto v = options::extract_option_values(options_values, "rootfs");
        if (v.size() > 1) {
            printf("Ignoring '--rootfs' options after the first.");
        }
        opt_rootfs = v.front();
    }

    if (options::option_value_exists(options_values, "mount-fs")) {
        auto mounts = options::extract_option_values(options_values, "mount-fs");
        for (auto m : mounts) {
            std::vector<std::string> tmp;
            boost::split(tmp, m, boost::is_any_of(","), boost::token_compress_on);
            if (tmp.size() != 3 || tmp[0].empty() || tmp[1].empty() || tmp[2].empty()) {
                printf("Ignoring value: '%s' for option mount-fs, expected in format: <fs_type,url,path>\n", m.c_str());
                continue;
            }
            mntent mount = {
                .mnt_fsname = strdup(tmp[1].c_str()),
                .mnt_dir = strdup(tmp[2].c_str()),
                .mnt_type = strdup(tmp[0].c_str()),
                .mnt_opts = nullptr
            };
            opt_mount_fs.push_back(mount);
        }
    }

    if (options::option_value_exists(options_values, "env")) {
        for (auto t : options::extract_option_values(options_values, "env")) {
            debug("Setting in environment: %s\n", t);
            putenv(strdup(t.c_str()));
        }
    }

    if (options::option_value_exists(options_values, "cwd")) {
        auto v = options::extract_option_values(options_values, "cwd");
        if (v.size() > 1) {
            printf("Ignoring '--cwd' options after the first.");
        }
        opt_chdir = v.front();
    }

    if (options::option_value_exists(options_values, "ip")) {
        opt_ip = options::extract_option_values(options_values, "ip");
    }

    if (options::option_value_exists(options_values, "defaultgw")) {
        for (auto t : options::extract_option_values(options_values, "defaultgw")) {
            opt_defaultgw.push_back(t);
        }
    }

    if (options::option_value_exists(options_values, "nameserver")) {
        for (auto t : options::extract_option_values(options_values, "nameserver")) {
            opt_nameserver.push_back(t);
        }
    }

    if (options::option_value_exists(options_values, "redirect")) {
        opt_redirect = options::extract_option_value(options_values, "redirect");
    }

    if (options::option_value_exists(options_values, "delay")) {
        boot_delay = std::chrono::duration_cast<std::chrono::nanoseconds>(1_s * options::extract_option_float_value(options_values, "delay", handle_parse_error));
    } else {
        boot_delay = std::chrono::duration_cast<std::chrono::nanoseconds>(1_s * 0.0f);
    }

    if (extract_option_flag(options_values, "nopci")) {
        opt_pci_disabled = true;
    }

    if (!options_values.empty()) {
        for (auto other_option : options_values) {
            std::cout << "unrecognized option: " << other_option.first << std::endl;
        }

        usage();
        osv::poweroff();
    }
}

// return the std::string and the commands_args poiting to them as a move
std::vector<std::vector<std::string> > prepare_commands(char* app_cmdline)
{
    std::vector<std::vector<std::string> > commands;
    bool ok;

    printf("Cmdline: %s\n", app_cmdline);
    commands = osv::parse_command_line(app_cmdline, ok);

    if (!ok) {
        puts("Failed to parse command line.");
        osv::poweroff();
    }
    if (commands.size() == 0) {
        puts("This image has an empty command line. Nothing to run.");
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

static void stop_all_remaining_app_threads()
{
    while(!application::unsafe_stop_and_abandon_other_threads()) {
        usleep(100000);
    }
}

static void load_zfs_library(std::function<void()> on_load_fun = nullptr)
{
    // Load and initialize ZFS filesystem driver implemented in libsolaris.so
    const auto libsolaris_path = "/usr/lib/fs/libsolaris.so";
    if (dlopen(libsolaris_path, RTLD_LAZY)) {
        if (on_load_fun) {
           on_load_fun();
        }
    } else {
        debug("Could not load and/or initialize %s.\n", libsolaris_path);
    }
}

static void load_zfs_library_and_mount_zfs_root(const char* mount_error_msg, bool pivot_when_error = false)
{
    load_zfs_library([mount_error_msg, pivot_when_error]() {
        zfsdev::zfsdev_init();
        auto error = mount_zfs_rootfs(opt_pivot, opt_extra_zfs_pools);
        if (error) {
            debug(mount_error_msg);
            if (pivot_when_error) {
                // Continue with ramfs (already mounted)
                // TODO: Avoid the hack of using pivot_rootfs() just for
                // mounting the fstab entries.
                pivot_rootfs("/");
            }
        } else {
            bsd_shrinker_init();
            boot_time.event("ZFS mounted");
        }
    });
}

void* do_main_thread(void *_main_args)
{
    auto app_cmdline = static_cast<char*>(_main_args);

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
        unmount_devfs();

        if (opt_rootfs.compare("rofs") == 0) {
            auto error = mount_rofs_rootfs(opt_pivot);
            if (error) {
                debug("Could not mount rofs root filesystem.\n");
            }

            if (opt_disable_rofs_cache) {
                debug("Disabling ROFS memory cache.\n");
                rofs_disable_cache();
            }
            boot_time.event("ROFS mounted");
        } else if (opt_rootfs.compare("zfs") == 0) {
            load_zfs_library_and_mount_zfs_root("Could not mount zfs root filesystem.\n");
        } else if (opt_rootfs.compare("ramfs") == 0) {
            // NOTE: The ramfs is already mounted, we just need to mount fstab
            // entries. That's the only difference between this and --nomount.

            // TODO: Avoid the hack of using pivot_rootfs() just for mounting
            // the fstab entries.
            pivot_rootfs("/");
        } else if (opt_rootfs.compare("virtiofs") == 0) {
            auto error = mount_virtiofs_rootfs(opt_pivot);
            if (error) {
                debug("Could not mount virtiofs root filesystem.\n");
            }

            boot_time.event("Virtio-fs mounted");
        } else {
            // Auto-discovery: try rofs -> virtio-fs -> ZFS
            if (mount_rofs_rootfs(opt_pivot) == 0) {
                if (opt_disable_rofs_cache) {
                    debug("Disabling ROFS memory cache.\n");
                    rofs_disable_cache();
                }
                boot_time.event("ROFS mounted");
            } else if (mount_virtiofs_rootfs(opt_pivot) == 0) {
                boot_time.event("Virtio-fs mounted");
            } else {
                load_zfs_library_and_mount_zfs_root("Could not mount zfs root filesystem (while auto-discovering).\n", true);
            }
        }
    }

    if (opt_preload_zfs_library) {
        load_zfs_library();
    }

#ifdef INET6
    // Enable IPv6 StateLess Address AutoConfiguration (SLAAC)
    osv::set_ipv6_accept_rtadv(true);
#endif

    bool has_if = false;
    osv::for_each_if([&has_if] (std::string if_name) {
        if (if_name == "lo0")
            return;

        has_if = true;

        if (osv::ifup(if_name) != 0)
            debug("Could not initialize network interface.\n");

        if (opt_ip.size() == 0) {
            // Start DHCP by default and wait for an IP
            if (osv::if_add_addr(if_name, "0.0.0.0", "255.255.255.0") != 0)
                debug("Could not add 0.0.0.0 IP to interface.\n");
        }
    });
    if (has_if) {
        if (opt_ip.size() == 0) {
            dhcp_start(true);
        } else {
            // Add interface IP addresses
            for (auto t : opt_ip) {
                std::vector<std::string> tmp;
                boost::split(tmp, t, boost::is_any_of(" ,/"), boost::token_compress_on);
                if (tmp.size() != 3)
                    abort("incorrect parameter on --ip");

                printf("%s: %s %s\n",tmp[0].c_str(),tmp[1].c_str(), tmp[2].c_str());

                if (osv::if_add_addr(tmp[0], tmp[1], tmp[2]) != 0)
                    debug("Could not add IP address to interface.\n");
            }
            // Add default gateway routes
            // One default route is allowed for IPv4 and one for IPv6
            if (opt_defaultgw.size() != 0) {
                bool has_defaultgw_v4=false, has_defaultgw_v6=false;
                for (auto t : opt_defaultgw) {
                    auto addr = boost::asio::ip::address::from_string(t);
                    if (addr.is_v4()) {
                        if (!has_defaultgw_v4) {
                            osv_route_add_network("0.0.0.0",
                                                  "0.0.0.0",
                                                  t.c_str());
                            has_defaultgw_v4 = true;
                        }
                    }
                    else {
                        if (!has_defaultgw_v6) {
                            osv_route_add_network("::",
                                                  "::",
                                                  t.c_str());
                            has_defaultgw_v6 = true;
                        }
                    }
                }
            }
            // Add nameserver addresses
            if (opt_nameserver.size() != 0) {
                std::vector<boost::asio::ip::address> dns_servers;
                for (auto t : opt_nameserver) {
                    auto addr = boost::asio::ip::address::from_string(t);
                    dns_servers.push_back(addr);
                }
                if (!dns_servers.empty()) {
                    osv::set_dns_config(dns_servers, std::vector<std::string>());
                }
            }
        }
    }

    std::string if_ip;
    auto nr_ips = 0;

    osv::for_each_if([&](std::string if_name) {
        if (if_name == "lo0")
            return;
        if_ip = osv::if_ip(if_name);
        nr_ips++;
    });
    if (nr_ips == 1) {
       setenv("OSV_IP", if_ip.c_str(), 1);
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
#ifdef __x86_64__
    // Some hypervisors like firecracker when booting OSv
    // look for this write to this port as a signal of end of
    // boot time.
    processor::outb(123, 0x3f0);
#endif /* __x86_64__ */

    if (opt_bootchart) {
        boot_time.print_chart();
    } else {
        boot_time.print_total_time();
    }

    if (!opt_redirect.empty()) {
        // redirect stdout and stdin to the given file, instead of the console
        // use ">>filename" to append, instead of replace, to a file.
        bool append = (opt_redirect.substr(0, 2) == ">>");
        auto fn = opt_redirect.substr(append ? 2 : 0);
        int fd = open(fn.c_str(),
                O_WRONLY | O_CREAT | (append ? O_APPEND: O_TRUNC), 777);
        if (fd < 0) {
            perror("output redirection failed");
        } else {
            std::cout << (append ? "Appending" : "Writing") <<
                    " stdout and stderr to " << fn << "\n";
            close(1);
            close(2);
            dup(fd);
            dup(fd);
        }
    }

    auto commands = prepare_commands(app_cmdline);

    // Run command lines in /init/* before the manual command line
    if (opt_init) {
        std::vector<std::vector<std::string>> init_commands;
        struct dirent **namelist = nullptr;
        int count = scandir("/init", &namelist, NULL, alphasort);
        for (int i = 0; i < count; i++) {
            if (!strcmp(".", namelist[i]->d_name) ||
                    !strcmp("..", namelist[i]->d_name)) {
                free(namelist[i]);
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
        commands.insert(commands.begin(),
                 init_commands.begin(), init_commands.end());
    }

    // run each payload in order
    // Our parse_command_line() leaves at the end of each command a delimiter,
    // can be '&' if we need to run this command in a new thread, or ';' or
    // empty otherwise, to run in this thread. '&!' is the same as '&', but
    // doesn't wait for the thread to finish before exiting OSv.
    std::vector<shared_app_t> detached;
    std::vector<shared_app_t> bg;
    for (auto &it : commands) {
        std::vector<std::string> newvec(it.begin(), std::prev(it.end()));
        auto suffix = it.back();
        try {
            bool background = (suffix == "&") || (suffix == "&!");

            shared_app_t app;
            if (suffix == "!") {
                app = application::run(newvec[0], newvec, false, nullptr, "main", stop_all_remaining_app_threads);
            } else {
                app = application::run(newvec);
            }

            if (suffix == "&!") {
                detached.push_back(app);
            } else if (!background) {
                app->join();
            } else {
                bg.push_back(app);
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
    }

    application::join_all();
    return nullptr;
}

void main_cont(int loader_argc, char** loader_argv)
{
    osv::firmware_probe();

    debug("Firmware vendor: %s\n", osv::firmware_vendor().c_str());

    elf::create_main_program();

    std::vector<std::vector<std::string> > cmds;

    parse_options(loader_argc, loader_argv);

    setenv("OSV_VERSION", osv::version().c_str(), 1);

#if CONF_drivers_xen
    xen::irq_init();
#endif
    smp_launch();
    setenv("OSV_CPUS", std::to_string(sched::cpus.size()).c_str(), 1);
    boot_time.event("SMP launched");

    auto end = osv::clock::uptime::now() + boot_delay;
    while (end > osv::clock::uptime::now()) {
        // spin
    }

    memory::enable_debug_allocator();

#ifdef __x86_64__
#if CONF_drivers_acpi
    acpi::init();
#endif
#endif /* __x86_64__ */

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
    elf::setup_missing_symbols_detector();

    bsd_init();

    vfs_init();
    boot_time.event("VFS initialized");
    //ramdisk_init();

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

    pthread_t pthread;
    // run the payload in a pthread, so pthread_self() etc. work
    // start do_main_thread unpinned (== pinned to all cpus)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (size_t ii=0; ii<sched::cpus.size(); ii++) {
        CPU_SET(ii, &cpuset);
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
    pthread_create(&pthread, &attr, do_main_thread, (void *) __app_cmdline);
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

int __loader_argc = 0;
char** __loader_argv = nullptr;
char* __app_cmdline = nullptr;
