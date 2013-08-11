
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
#include "ioapic.hh"

#include "drivers/driver.hh"
#include "drivers/virtio-net.hh"
#include "drivers/virtio-blk.hh"

#include "sched.hh"
#include "drivers/clock.hh"
#include "drivers/clockevent.hh"
#include "drivers/console.hh"
#include "barrier.hh"
#include "arch.hh"
#include "osv/trace.hh"
#include <osv/power.hh>
#include <osv/rcu.hh>
#include "mempool.hh"

using namespace osv;

asm(".pushsection \".debug_gdb_scripts\", \"MS\",@progbits,1 \n"
    ".byte 1 \n"
    ".asciz \"scripts/loader.py\" \n"
    ".popsection \n");

namespace {

    void test_locale()
    {
	auto loc = std::locale();
	auto &fac = std::use_facet<std::ctype<char>>(loc);
	bool ok = fac.is(std::ctype_base::digit, '3')
	    && !fac.is(std::ctype_base::digit, 'x');
	debug(ok ? "locale works\n" : "locale fails\n");
	//asm volatile ("1: jmp 1b");
    }

}

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
    void mount_usr(void);
    void ramdisk_init(void);
}


void disable_pic()
{
    outb(0xff, 0x21);
    outb(0xff, 0xa1);
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

elf::program* prog;

int main(int ac, char **av)
{
    debug("Loader Copyright 2013 Cloudius Systems\n");

    test_locale();
    idt.load_on_cpu();
    void main_cont(int ac, char** av);
    sched::init([=] { main_cont(ac, av); });
}

static bool opt_leak = false;
static bool opt_noshutdown = false;

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

    bpo::options_description desc("osv options:\n");
    desc.add_options()
        ("help", "show help text\n")
        ("trace", bpo::value<std::vector<std::string>>(), "tracepoints to enable\n")
        ("leak", "start leak detector after boot\n")
        ("noshutdown", "continue running after main() returns\n")
    ;
    bpo::variables_map vars;
    // don't allow --foo bar (require --foo=bar) so we can find the first non-option
    // argument
    int style = bpos::unix_style & ~(bpos::long_allow_next | bpos::short_allow_next);
    bpo::store(bpo::parse_command_line(args.size(), args.data(), desc, style), vars);
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
    av += nr_options;
    ac -= nr_options;
    return std::make_tuple(ac, av);
}

struct argblock {
    int ac;
    char** av;
};

// Java uses this global variable (supplied by Glibc) to figure out the
// initial thread's stack end.
void *__libc_stack_end;

void run_main(elf::program *prog, struct argblock *args)
{
    auto av = args->av;
    auto ac = args->ac;
    auto obj = prog->add_object(av[0]);
    if (!obj) {
        debug("run_main(): cannot execute %s. Aborting.\n", av[0]);
        abort();
    }
    auto main = obj->lookup<void (int, char**)>("main");
    assert(main);

    if (opt_leak) {
        debug("Enabling leak detector.\n");
        memory::tracker_enabled = true;
    }

    __libc_stack_end = __builtin_frame_address(0);

    main(ac, av);
}

void* do_main_thread(void *_args)
{
    auto args = static_cast<argblock*>(_args);

    // Enumerate PCI devices
    pci::pci_device_enumeration();

    // Initialize all drivers
    hw::driver_manager* drvman = hw::driver_manager::instance();
    drvman->register_driver(virtio::virtio_blk::probe);
    drvman->register_driver(virtio::virtio_net::probe);
    drvman->load_all();
    drvman->list_drivers();


    mount_usr();

    run_main(prog, args);

    return nullptr;
}

namespace pthread_private {
    void init_detached_pthreads_reaper();
}

void main_cont(int ac, char** av)
{
    sched::preempt_disable();
    std::tie(ac, av) = parse_options(ac, av);
    ioapic::init();
    console::console_init();
    smp_launch();
    sched::preempt_enable();
    memory::enable_debug_allocator();
    enable_trace();
    sched::init_detached_threads_reaper();
    rcu_init();

    vfs_init();
    ramdisk_init();

    filesystem fs;

    net_init();

    processor::sti();

    prog = new elf::program(fs);
    prog->set_search_path({"/", "/usr/lib"});

    pthread_t pthread;
    // run the payload in a pthread, so pthread_self() etc. work
    argblock args{ ac, av };
    pthread_create(&pthread, nullptr, do_main_thread, &args);
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
        debug("Powering off.\n");
        osv::poweroff();
    }
}

int __argc;
char** __argv;
