
#include "drivers/isa-serial.hh"
#include "fs/fs.hh"
#include <boost/format.hpp>
#include <cctype>
#include "elf.hh"
#include "tls.hh"
#include "msr.hh"
#include "exceptions.hh"
#include "debug.hh"
#include "drivers/pci.hh"
#include "drivers/device-factory.hh"
#include <jni.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
//#include <locale>

#include "drivers/virtio-net.hh"
#include "drivers/virtio-blk.hh"

#include "drivers/driver-factory.hh"
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

void setup_tls(elf::init_table inittab)
{
    static char tcb0[1 << 15] __attribute__((aligned(4096)));
    assert(inittab.tls_size + sizeof(thread_control_block) <= sizeof(tcb0));
    memcpy(tcb0, inittab.tls, inittab.tls_size);
    auto p = reinterpret_cast<thread_control_block*>(tcb0 + inittab.tls_size);
    p->self = p;
    processor::wrmsr(msr::IA32_FS_BASE, reinterpret_cast<uint64_t>(p));
}

extern "C" { void premain(); }

extern "C" { void vfs_init(void); }

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
    thread* t2;
};

void test_thread_1(test_threads_data& tt)
{
    while (test_ctr < 1000) {
        thread::wait_until([&] { return (test_ctr % 2) == 0; });
        ++test_ctr;
        if (tt.t2) {
            tt.t2->wake();
        }
    }
    tt.t1 = nullptr;
    tt.main->wake();
}

void test_thread_2(test_threads_data& tt)
{
    while (test_ctr < 1000) {
        thread::wait_until([&] { return (test_ctr % 2) == 1; });
        ++test_ctr;
        if (tt.t1) {
            tt.t1->wake();
        }
    }
    tt.t2 = nullptr;
    tt.main->wake();
}

void test_threads()
{
    test_threads_data tt;
    tt.main = thread::current();
    char stk1[10000], stk2[10000];
    tt.t1 = new thread([&] { test_thread_1(tt); }, { stk1, 10000 });
    tt.t2 = new thread([&] { test_thread_2(tt); }, { stk2, 10000 });

    thread::wait_until([&] { return test_ctr >= 1000; });
    debug("threading test succeeded");
}

int main(int ac, char **av)
{
    debug("Loader Copyright 2013 Unnamed");

    test_locale();
    idt.load_on_cpu();

    vfs_init();

    filesystem fs;

    disable_pic();
    processor::sti();

#if 1
    if (std::isdigit('1'))
	debug("isgidit(1) = ok");
    else
	debug("isgidit(1) = bad");
    if (!std::isdigit('x'))
	debug("isgidit(x) = ok");
    else
	debug("isgidit(x) = bad");
#if 0
    auto &fac = std::use_facet<std::ctype<char> >(std::locale("C"));
    if (fac.is(std::ctype<char>::digit, '1'))
	debug("facet works");
    else
	debug("facet !works");
#endif
    //while (true)
    //	;
#endif

    elf::program prog(fs);
    sched::init(prog);
    void main_thread(elf::program& prog);
    static char main_stack[64*1024];
    new thread([&] { main_thread(prog); }, { main_stack, sizeof main_stack }, true);
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

void load_test(elf::program& prog, char *path)
{
    printf("running %s\n", path);

    prog.add_object(path);

    auto test_main
        = prog.lookup_function<int (int, const char **)>("main");
    std::string str = "test";
    const char *name = str.c_str();
    int ret = test_main(1, &name);
    if (ret)
        printf("failed.\n");
    else
        printf("ok.\n");

    prog.remove_object(path);
}


int load_tests(elf::program& prog)
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

#define JVM_PATH	"/usr/lib/jvm/jre/lib/amd64/server/libjvm.so"

void start_jvm(elf::program& prog)
{
    prog.add_object(JVM_PATH);
 
    auto JNI_GetDefaultJavaVMInitArgs
        = prog.lookup_function<void (void*)>("JNI_GetDefaultJavaVMInitArgs");
    JavaVMInitArgs vm_args = {};
    vm_args.version = JNI_VERSION_1_6;
    JNI_GetDefaultJavaVMInitArgs(&vm_args);
    vm_args.nOptions = 1;
    vm_args.options = new JavaVMOption[1];
    vm_args.options[0].optionString = strdup("-Djava.class.path=/tests");

    auto JNI_CreateJavaVM
        = prog.lookup_function<jint (JavaVM**, JNIEnv**, void*)>("JNI_CreateJavaVM");
    JavaVM* jvm = nullptr;
    JNIEnv *env;

    auto ret = JNI_CreateJavaVM(&jvm, &env, &vm_args);
    assert(ret == 0);
    auto mainclass = env->FindClass("Hello");
    auto mainmethod = env->GetStaticMethodID(mainclass, "main", "([Ljava/lang/String;)V");
    env->CallStaticVoidMethod(mainclass, mainmethod, nullptr);
}

void* do_main_thread(void* pprog)
{
    auto& prog = *static_cast<elf::program*>(pprog);
    test_threads();
    test_clock_events();

    pci::pci_devices_print();
    pci::pci_device_enumeration();
    DeviceFactory::Instance()->DumpDevices();

    Driver *vnet = new virtio::virtio_net();
    DriverFactory::Instance()->RegisterDriver(vnet);

    Driver *vblk = new virtio::virtio_blk();
    DriverFactory::Instance()->RegisterDriver(vblk);

    DeviceFactory::Instance()->InitializeDrivers();

    DriverFactory::Instance()->Destroy();

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

//    load_tests(prog);
    start_jvm(prog);

    while (true)
	;
    return nullptr;
}

void main_thread(elf::program& prog)
{
    pthread_t pthread;
    // run the payload in a pthread, so pthread_self() etc. work
    pthread_create(&pthread, nullptr, do_main_thread, &prog);
    sched::thread::wait_until([] { return false; });
}

int __argc;
char** __argv;
