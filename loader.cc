
#include "drivers/isa-serial.hh"
#include "fs/fs.hh"
#include <bsd/net.hh>
#include <boost/format.hpp>
#include <cctype>
#include "elf.hh"
#include "tls.hh"
#include "msr.hh"
#include "exceptions.hh"
#include "debug.hh"
#include "drivers/pci.hh"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "smp.hh"
//#include <locale>

#include "drivers/driver.hh"
#include "drivers/virtio-net.hh"
#include "drivers/virtio-blk.hh"

#include "sched.hh"
#include "drivers/clock.hh"
#include "drivers/clockevent.hh"
#include "barrier.hh"

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
	debug(ok ? "locale works" : "locale fails");
	//asm volatile ("1: jmp 1b");
    }

}

elf::Elf64_Ehdr* elf_header;
elf::tls_data tls_data;

void setup_tls(elf::init_table inittab)
{
    tls_data = inittab.tls;
    extern char tcb0[]; // defined by linker script
    memcpy(tcb0, inittab.tls.start, inittab.tls.size);
    auto p = reinterpret_cast<thread_control_block*>(tcb0 + inittab.tls.size);
    p->self = p;
    processor::wrmsr(msr::IA32_FS_BASE, reinterpret_cast<uint64_t>(p));
}

extern "C" {
    void premain();
    void vfs_init(void);
    void ramdisk_init(void);
}


void premain()
{
    auto inittab = elf::get_init(elf_header);
    setup_tls(inittab);
    for (auto init = inittab.start; init < inittab.start + inittab.count; ++init) {
        (*init)();
    }
}

void disable_pic()
{
    outb(0xff, 0x21);
    outb(0xff, 0xa1);
}

static int test_ctr;

using sched::thread;

struct test_threads_data {
    thread* main;
    thread* t1;
    bool t1ok;
    thread* t2;
    bool t2ok;
};

void test_thread_1(test_threads_data& tt)
{
    while (test_ctr < 1000) {
        thread::wait_until([&] { return (test_ctr % 2) == 0; });
        ++test_ctr;
        if (tt.t2ok) {
            tt.t2->wake();
        }
    }
    tt.t1ok = false;
    tt.main->wake();
}

void test_thread_2(test_threads_data& tt)
{
    while (test_ctr < 1000) {
        thread::wait_until([&] { return (test_ctr % 2) == 1; });
        ++test_ctr;
        if (tt.t1ok) {
            tt.t1->wake();
        }
    }
    tt.t2ok = false;
    tt.main->wake();
}

void test_threads()
{
    test_threads_data tt;
    tt.main = thread::current();
    char stk1[10000], stk2[10000];
    tt.t1ok = tt.t2ok = true;
    tt.t1 = new thread([&] { test_thread_1(tt); }, { stk1, 10000 });
    tt.t2 = new thread([&] { test_thread_2(tt); }, { stk2, 10000 });
    thread::wait_until([&] { return test_ctr >= 1000; });
    delete tt.t1;
    delete tt.t2;
    debug("threading test succeeded");
}

elf::program* prog;

int main(int ac, char **av)
{
    debug("Loader Copyright 2013 Unnamed");

    test_locale();
    idt.load_on_cpu();
    smp_init();
    void main_cont(int ac, char** av);
    sched::init(tls_data, [=] { main_cont(ac, av); });
}

void main_cont(int ac, char** av)
{
    smp_launch();

    vfs_init();
    ramdisk_init();

    filesystem fs;

    test_mbuf();

    disable_pic();
    processor::sti();

    prog = new elf::program(fs);
    void main_thread(int ac, char** av);
    main_thread(ac, av);
}

void test_clock_events()
{
    struct test_callback : public clock_event_callback {
        test_callback() : n() {}
        virtual void fired() { t[n++] = clock::get()->time(); }
        unsigned n;
        u64 t[20];
    };
    test_callback t;
    clock_event_callback* old_callback = clock_event->callback();
    clock_event->set_callback(&t);
    for (unsigned i = 0; i < 10; ++i) {
        clock_event->set(clock::get()->time() + 1000000);
        while (t.n == i) {
            barrier();
        }
    }
    clock_event->set_callback(nullptr);
    for (unsigned i = 0; i < 10; ++i) {
        debug(fmt("clock_event: %d") % t.t[i]);
    }
    clock_event->set_callback(old_callback);
}

struct argblock {
    int ac;
    char** av;
};

void load_test(elf::program *prog, char *path)
{
    printf("running %s\n", path);

    prog->add_object(path);

    auto test_main
        = prog->lookup_function<int (int, const char **)>("main");
    std::string str = "test";
    const char *name = str.c_str();
    int ret = test_main(1, &name);
    if (ret)
        printf("failed.\n");
    else
        printf("ok.\n");

    prog->remove_object(path);
}


int load_tests(elf::program *prog, struct argblock *args)
{
#define TESTDIR		"/tests"
    DIR *dir = opendir(TESTDIR);
    char path[PATH_MAX];
    struct dirent *d;
    struct stat st;

    if (!dir) {
        perror("failed to open testdir");
        return EXIT_FAILURE;
    }

    while ((d = readdir(dir))) {
        if (strcmp(d->d_name, ".") == 0 ||
            strcmp(d->d_name, "..") == 0)
           continue;

        snprintf(path, PATH_MAX, "%s/%s", TESTDIR, d->d_name);
        if (__xstat(1, path, &st) < 0) {
            printf("failed to stat %s\n", path);
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            printf("ignoring %s, not a regular file\n", path);
            continue;
        }
        load_test(prog, path);
    }
    if (closedir(dir) < 0) {
        perror("failed to close testdir");
        return EXIT_FAILURE;
    }

    return 0;
}

void run_main(elf::program *prog, struct argblock *args)
{
    auto av = args->av;
    auto ac = args->ac;
    prog->add_object(av[0]);
    ++av, --ac;
    auto main = prog->lookup_function<void (int, char**)>("main");
    main(ac, av);
}

void* do_main_thread(void *_args)
{
    auto args = static_cast<argblock*>(_args);
    test_threads();
    test_clock_events();

    // Enumerate PCI devices
    pci::pci_devices_print();
    pci::pci_device_enumeration();

    // List all devices
    hw::device_manager::instance()->list_devices();

    // Initialize all drivers
    hw::driver_manager* drvman = hw::driver_manager::instance();
    drvman->register_driver(new virtio::virtio_blk(0));
    drvman->register_driver(new virtio::virtio_blk(1));
    drvman->register_driver(new virtio::virtio_net());
    drvman->load_all();
    drvman->list_drivers();

    auto t1 = clock::get()->time();
    auto t2 = clock::get()->time();
    debug(fmt("clock@t1 %1%") % t1);
    debug(fmt("clock@t2 %1%") % t2);

    timespec ts = {};
    ts.tv_nsec = 100;
    t1 = clock::get()->time();
    nanosleep(&ts, nullptr);
    t2 = clock::get()->time();
    debug(fmt("nanosleep(100) -> %d") % (t2 - t1));
    ts.tv_nsec = 100000;
    t1 = clock::get()->time();
    nanosleep(&ts, nullptr);
    t2 = clock::get()->time();
    debug(fmt("nanosleep(100000) -> %d") % (t2 - t1));

//    load_tests(prog, args);
    run_main(prog, args);

    while (true)
	;
    return nullptr;
}

void main_thread(int ac, char **av)
{
    pthread_t pthread;
    // run the payload in a pthread, so pthread_self() etc. work
    argblock args{ ac, av };
    pthread_create(&pthread, nullptr, do_main_thread, &args);
    sched::thread::wait_until([] { return false; });
}

int __argc;
char** __argv;
