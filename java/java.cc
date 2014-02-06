/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/elf.hh>
#include <jni.h>
#include <string.h>
#include <osv/debug.hh>
#include "jvm_balloon.hh"
#include <osv/mempool.hh>

// java.so is similar to the standard "java" command line launcher in Linux.
//
// This program does very little - basically it starts the JVM and asks it
// to run a fixed class, RunJava, which parses the command line parameters,
// sets up the class path, and runs the jar or class specified in these
// parameters.

#define JVM_PATH        "/usr/lib/jvm/jre/lib/amd64/server/libjvm.so"
#define RUNJAVA_PATH    "/java/runjava.jar"
#define RUNJAVA         "io/osv/RunJava"    // separated by slashes, not dots

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
           starts_with(arg, "-agentlib");
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

extern "C"
int main(int argc, char **argv)
{
    auto prog = elf::get_program();
    // The JVM library remains loaded as long as jvm_so is in scope.
    auto jvm_so = prog->get_library(JVM_PATH);

    auto JNI_GetDefaultJavaVMInitArgs
        = prog->lookup_function<void (void*)>("JNI_GetDefaultJavaVMInitArgs");
    JavaVMInitArgs vm_args = {};
    vm_args.version = JNI_VERSION_1_6;
    JNI_GetDefaultJavaVMInitArgs(&vm_args);

    std::vector<JavaVMOption> options;
    options.push_back(mkoption("-Djava.class.path=" RUNJAVA_PATH));
    options.push_back(mkoption("-Djava.system.class.loader=io.osv.OsvSystemClassLoader"));
    options.push_back(mkoption("-Djava.util.logging.manager=io.osv.jul.IsolatingLogManager"));

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

    if (!has_xmx) {
        // FIXME: We should estimate how much memory the JVM itself is going to use
        auto heap_size = memory::stats::free() >> 20;
        options.push_back(mkoption("-Xmx%dM", heap_size));
        if (!has_xms) {
            options.push_back(mkoption("-Xms%dM", heap_size));
        }
        debugf("Autotuning JVM heap size to %d MB\n", heap_size);
    }

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

    auto mainclass = env->FindClass(RUNJAVA);
    if (!mainclass) {
        if (env->ExceptionOccurred()) {
            std::cerr << "java.so: Failed to load " << RUNJAVA << "\n";
            env->ExceptionDescribe();
            env->ExceptionClear();
        } else {
            std::cerr << "java.so: Can't find class " << RUNJAVA << " in " << RUNJAVA_PATH << ".\n";
        }
        return 1;
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

    // Manually setting the heap size is viewed as a declaration of intent. In
    // that case, we'll leave the user alone. This may be revisited in the
    // future, but it is certainly the safest option.
    std::unique_ptr<jvm_balloon_shrinker>
        balloon(has_xmx ? nullptr : new jvm_balloon_shrinker(jvm));

    env->CallStaticVoidMethod(mainclass, mainmethod, args);

    // DestroyJavaVM() waits for all all non-daemon threads to end, and
    // only then destroys the JVM.
    jvm->DetachCurrentThread();
    jvm->DestroyJavaVM();
    return 0;
}
