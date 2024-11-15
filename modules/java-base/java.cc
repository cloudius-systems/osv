/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/elf.hh>
#include <jni.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#include <regex>
#include <osv/debug.hh>
#include <osv/kernel_config_memory_jvm_balloon.h>
#if CONF_memory_jvm_balloon
#include "balloon/jvm_balloon.hh"
#endif
#include <osv/mempool.hh>
#include "jvm/java_api.hh"
#include "osv/version.hh"
#include "jvm/jni_helpers.hh"
#include <osv/defer.hh>
#include <osv/app.hh>
#include <iostream>

#if CONF_memory_jvm_balloon
extern size_t jvm_heap_size;
#endif

// java.so is similar to the standard "java" command line launcher in Linux.
//
// This program does very little - basically it starts the JVM and asks it
// to run a fixed class, RunJava, which parses the command line parameters,
// sets up the class path, and runs the jar or class specified in these
// parameters.

#ifdef __aarch64__
#define JVM_PATH         "/usr/lib/jvm/jre/lib/aarch64/server/libjvm.so"
#else
#define JVM_PATH         "/usr/lib/jvm/jre/lib/amd64/server/libjvm.so"
#endif
#define JVM9_PATH        "/usr/lib/jvm/java/lib/server/libjvm.so"

#if defined(RUN_JAVA_NON_ISOLATED)
#define RUNJAVA_JAR_PATH "/java/runjava-non-isolated.jar"
#define RUNJAVA          "io/osv/nonisolated/RunNonIsolatedJvmApp"    // separated by slashes, not dots
#else
#define RUNJAVA_JAR_PATH "/java/runjava-isolated.jar"
#define RUNJAVA          "io/osv/isolated/RunIsolatedJvmApp"    // separated by slashes, not dots
#endif

JavaVMOption mkoption(const char* s)
{
    JavaVMOption opt = {};
    opt.optionString = strdup(s);
    return opt;
}

JavaVMOption mkoption(std::string str)
{
    return mkoption(str.c_str());
}

template <typename... args>
JavaVMOption mkoption(const char *fmt, args... as)
{
    return mkoption(osv::sprintf(fmt, as...));
}

inline bool starts_with(const char *s, const char *prefix)
{
    return !strncmp(s, prefix, strlen(prefix));
}

static bool is_jvm_option(const char *arg) {
    return starts_with(arg, "-verbose") ||
           starts_with(arg, "-D") ||
           starts_with(arg, "-X") ||
           starts_with(arg, "-javaagent") ||
           starts_with(arg, "-agentlib") ||
           starts_with(arg, "-disableassertions") ||
           starts_with(arg, "-enableeassertions") ||
           starts_with(arg, "-da") ||
           starts_with(arg, "-ea") ||
           starts_with(arg, "-disablesystemassertions") ||
           starts_with(arg, "-enableesystemassertions") ||
           starts_with(arg, "-dsa") ||
           starts_with(arg, "-esa");
}

static void mark_heap_option(char **arg, int index, int &has_xms, int &has_xmx)
{
    if (starts_with(arg[index], "-Xms") && !has_xms) {
        has_xms = index;
    }

    if (starts_with(arg[index], "-Xmx") && !has_xmx) {
        has_xmx = index;
    }

    if (starts_with(arg[index], "-mx")) {
        has_xmx = index;
    }

    if (starts_with(arg[index], "-ms")) {
        has_xms = index;
    }
}

static void on_vm_stop(JNIEnv *env, jclass clz) {
    attached_env::set_jvm(nullptr);
}

static int java_main(int argc, char **argv)
{
    std::cout << "java.so: Starting JVM app using: " << RUNJAVA << "\n";

    auto prog = elf::get_program();
    // The JVM library remains loaded as long as jvm_so is in scope.
    auto jvm_so = prog->get_library(JVM_PATH);
    if(!jvm_so) {
        jvm_so = prog->get_library(JVM9_PATH);
    }

    auto JNI_GetDefaultJavaVMInitArgs
        = prog->lookup_function<void (void*)>("JNI_GetDefaultJavaVMInitArgs");
    JavaVMInitArgs vm_args = {};
    vm_args.version = JNI_VERSION_1_6;
    JNI_GetDefaultJavaVMInitArgs(&vm_args);

    std::vector<JavaVMOption> options;
    options.push_back(mkoption("-Djava.class.path=%s", RUNJAVA_JAR_PATH));

#if defined(RUN_JAVA_NON_ISOLATED)
    std::cout << "java.so: Setting Java system classloader to NonIsolatingOsvSystemClassLoader" << "\n";
    options.push_back(mkoption("-Djava.system.class.loader=io.osv.nonisolated.NonIsolatingOsvSystemClassLoader"));
#else
    std::cout << "java.so: Setting Java system classloader to IsolatingOsvSystemClassLoader and logging manager to IsolatingLogManager" << "\n";
    options.push_back(mkoption("-Djava.system.class.loader=io.osv.isolated.IsolatingOsvSystemClassLoader"));
    options.push_back(mkoption("-Djava.util.logging.manager=io.osv.jul.IsolatingLogManager"));
#endif

    options.push_back(mkoption("-Dosv.version=" + osv::version()));

    {
        const std::string path("/usr/lib/jvm/agents/autoload");

        std::shared_ptr<DIR> dir(opendir(path.c_str()), [](DIR * d) {
            if (d != 0) {
                closedir(d);
            }
        });

        std::regex sox(".*\\.so");

        dirent d, *e = nullptr;
        while (dir && !readdir_r(dir.get(), &d, &e) && e) {
            if (std::regex_match(e->d_name, sox)) {
                options.push_back(mkoption("-agentpath:" + path + "/" + e->d_name));
            }
        }
    }

    int orig_argc = argc;
    int has_xms = 0, has_xmx = 0;

    for (int i = 1; i < orig_argc; i++) {
        // We are not supposed to look for verbose options after -jar
        // or class name. From that point on, they are user provided
        if (!strcmp(argv[i], "-jar") || !starts_with(argv[i], "-"))
            break;

        mark_heap_option(argv, i, has_xms, has_xmx);

        // Pass some options directly to the JVM
        if (is_jvm_option(argv[i])) {
            options.push_back(mkoption(argv[i]));
            argv[i] = NULL; // so we don't pass it to RunJava
            argc--;
        }
    }

#if 0
    size_t auto_heap = 0;
    // Do not use total(), since that won't reflect the whole memory for the
    // machine. It then becomes counter intuitive to tell the user what is the
    // minimum he has to set to balloon
    if (!has_xmx && (memory::phys_mem_size >= memory::balloon_min_memory)) {
        auto_heap = std::min(memory::stats::free(), memory::stats::max_no_reclaim()) >> 20;
        options.push_back(mkoption("-Xmx%dM", auto_heap));
        if (!has_xms) {
            options.push_back(mkoption("-Xms%dM", auto_heap));
        }
        auto_heap <<= 20;
        jvm_heap_size = auto_heap;
    }
#endif

    vm_args.nOptions = options.size();
    vm_args.options = options.data();

    auto JNI_CreateJavaVM
        = prog->lookup_function<jint (JavaVM**, JNIEnv**, void*)>("JNI_CreateJavaVM");
    if (!JNI_CreateJavaVM) {
        std::cerr << "java.so: failed looking up JNI_CreateJavaVM()\n";
        return 1;
    }

    JavaVM* jvm = nullptr;
    JNIEnv *env;
    if (JNI_CreateJavaVM(&jvm, &env, &vm_args) != 0) {
        std::cerr << "java.so: Can't create VM.\n";
        return 1;
    }

    auto cleanup = defer([jvm] {
        // DestroyJavaVM() waits for all all non-daemon threads to end, and
        // only then destroys the JVM.
        jvm->DetachCurrentThread();
        jvm->DestroyJavaVM();
    });

#if CONF_memory_jvm_balloon
    jvm_heap_size = 0;
#endif

    java_api::set(jvm);
    attached_env::set_jvm(jvm);

    auto mainclass = env->FindClass(RUNJAVA);
    if (!mainclass) {
        if (env->ExceptionOccurred()) {
            std::cerr << "java.so: Failed to load " << RUNJAVA << "\n";
            env->ExceptionDescribe();
            env->ExceptionClear();
        } else {
            std::cerr << "java.so: Can't find class " << RUNJAVA << ".\n";
        }
        return 1;
    }

    {
        JNINativeMethod rnm = { (char*)"onVMStop", (char*)"()V", (void *)on_vm_stop };
        env->RegisterNatives(mainclass, &rnm, 1);
    }

    auto mainmethod = env->GetStaticMethodID(mainclass, "main", "([Ljava/lang/String;)V");
    if (!mainmethod) {
        std::cerr << "java.so: Can't find main() in class " << RUNJAVA << ".\n";
        return 1;
    }

    auto stringclass = env->FindClass("java/lang/String");
    auto args = env->NewObjectArray(argc-1, stringclass, nullptr);

    int index = 0;
    for (int i = 1; i < orig_argc; i++) {
        if (!argv[i])
            continue;
        env->SetObjectArrayElement(args, index++, env->NewStringUTF(argv[i]));
    }

#if CONF_memory_jvm_balloon
    // Manually setting the heap size is viewed as a declaration of intent. In
    // that case, we'll leave the user alone. This may be revisited in the
    // future, but it is certainly the safest option.
    std::unique_ptr<memory::jvm_balloon_api_impl>
        balloon(auto_heap == 0 ? nullptr : new memory::jvm_balloon_api_impl(jvm));
#endif

    env->CallStaticVoidMethod(mainclass, mainmethod, args);

    return 0;
}

extern "C"
int main(int argc, char **argv)
{
    int res = 0;
    std::thread t([&res](int argc, char **argv) {
        res = java_main(argc, argv);
    }, argc, argv);
    t.join();
    // Unfortunately, Java's jvm->DestroyJavaVM() doesn't fully clean up, and
    // leaves behind some detached threads such as GC threads and compilation
    // threads. If we return with those still existing, loader.cc will wait
    // (using application::join()) in vain for these threads to finish.
    // So let's stop these threads. This call is unsafe, in the sense we
    // assume that those renegade threads are not holding any critical
    // resources (e.g., not in the middle of I/O or memory allocation).
    while(!osv::application::unsafe_stop_and_abandon_other_threads()) {
        usleep(100000);
    }
    return res;
}
