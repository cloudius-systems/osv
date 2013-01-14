
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
//#include <locale>

#include "drivers/virtio-net.hh"

#include "drivers/driver-factory.hh"
#include "sched.hh"
#include "drivers/clock.hh"

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
    tt.t1 = new thread([&] { test_thread_1(tt); });
    tt.t2 = new thread([&] { test_thread_2(tt); });

    thread::wait_until([&] { return test_ctr >= 1000; });
    debug("threading test succeeded");
}

int main(int ac, char **av)
{
    IsaSerialConsole console;

    Debug::Instance()->setConsole(&console);
    debug("Loader Copyright 2013 Unnamed");

    test_locale();
    idt.load_on_cpu();
    processor::wrmsr(msr::IA32_APIC_BASE, 0xfee00000 | (3 << 10));

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
    new thread([&] { main_thread(prog); }, true);
}

#define TESTSO_PATH	"/usr/lib/tst-dir.so"

void load_test(elf::program& prog)
{
    prog.add(TESTSO_PATH);

    auto test_main
        = prog.lookup_function<int (void)>("test_main");
    int ret = test_main();
    if (ret)
    	debug("FAIL");
}

#define JVM_PATH	"/usr/lib/jvm/jre/lib/amd64/server/libjvm.so"

void start_jvm(elf::program& prog)
{
    prog.add(JVM_PATH);
 
    auto JNI_GetDefaultJavaVMInitArgs
        = prog.lookup_function<void (void*)>("JNI_GetDefaultJavaVMInitArgs");
    JavaVMInitArgs vm_args;
    vm_args.version = JNI_VERSION_1_6;
    JNI_GetDefaultJavaVMInitArgs(&vm_args);
    auto JNI_CreateJavaVM
        = prog.lookup_function<jint (JavaVM**, void**, void*)>("JNI_CreateJavaVM");
    JavaVM* jvm = nullptr;

    auto ret = JNI_CreateJavaVM(&jvm, nullptr, &vm_args);
    debug(fmt("JNI_CreateJavaVM() returned %1%") % ret);
}

void main_thread(elf::program& prog)
{
    test_threads();

    pci::pci_devices_print();
    pci::pci_device_enumeration();
    DeviceFactory::Instance()->DumpDevices();

    Driver *d = new virtio::virtio_net();
    DriverFactory::Instance()->RegisterDriver(d);

    DeviceFactory::Instance()->InitializeDrivers();

    auto t1 = clock::get()->time();
    auto t2 = clock::get()->time();
    debug(fmt("clock@t1 %1%") % t1);
    debug(fmt("clock@t2 %1%") % t2);

//    load_test(prog);
    start_jvm(prog);

    while (true)
	;
}
