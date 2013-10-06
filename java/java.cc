/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "elf.hh"
#include <jni.h>
#include <string.h>
#include "debug.hh"

// java.so is similar to the standard "java" command line launcher in Linux.
//
// This program does very little - basically it starts the JVM and asks it
// to run a fixed class, RunJava, which parses the command line parameters,
// sets up the class path, and runs the jar or class specified in these
// parameters.

extern elf::program* prog;

#define JVM_PATH        "/usr/lib/jvm/jre/lib/amd64/server/libjvm.so"
#define RUNJAVA_PATH    "/java/runjava.jar"
#define RUNJAVA         "io/osv/RunJava"    // separated by slashes, not dots

JavaVMOption mkoption(const char* s)
{
    JavaVMOption opt = {};
    opt.optionString = strdup(s);
    return opt;
}

inline bool starts_with(const char *s, const char *prefix)
{
    return !strncmp(s, prefix, strlen(prefix));
}

extern "C"
int main(int argc, char **argv)
{
    prog->add_object(JVM_PATH);

    auto JNI_GetDefaultJavaVMInitArgs
        = prog->lookup_function<void (void*)>("JNI_GetDefaultJavaVMInitArgs");
    JavaVMInitArgs vm_args = {};
    vm_args.version = JNI_VERSION_1_6;
    JNI_GetDefaultJavaVMInitArgs(&vm_args);

    std::vector<JavaVMOption> options;
    options.push_back(mkoption("-Djava.class.path=" RUNJAVA_PATH));

    int orig_argc = argc;
    for (int i = 1; i < orig_argc; i++) {
        // We are not supposed to look for verbose options after -jar
        // or class name. From that point on, they are user provided
        if (!strcmp(argv[i], "-jar") || !starts_with(argv[i], "-"))
            break;

        // Pass some options directly to the JVM
        if (starts_with(argv[i], "-verbose") || starts_with(argv[i], "-D") || starts_with(argv[i], "-X") || starts_with(argv[i], "-javaagent")) {
            options.push_back(mkoption(argv[i]));
            argv[i] = NULL; // so we don't pass it to RunJava
            argc--;
        }
    }
    vm_args.nOptions = options.size();
    vm_args.options = options.data();

    auto JNI_CreateJavaVM
        = prog->lookup_function<jint (JavaVM**, JNIEnv**, void*)>("JNI_CreateJavaVM");
    if (!JNI_CreateJavaVM) {
        debug("java.so: failed looking up JNI_CreateJavaVM()\n");
        abort();
    }

    JavaVM* jvm = nullptr;
    JNIEnv *env;
    if (JNI_CreateJavaVM(&jvm, &env, &vm_args) != 0) {
        debug("java.so: Can't create VM.\n");
        abort();
    }
    auto mainclass = env->FindClass(RUNJAVA);
    if (!mainclass) {
        debug("java.so: Can't find class %s in %s.\n", RUNJAVA, RUNJAVA_PATH);
        abort();
    }

    auto mainmethod = env->GetStaticMethodID(mainclass, "main", "([Ljava/lang/String;)V");
    if (!mainmethod) {
        debug("java.so: Can't find main() in class %s.\n", RUNJAVA);
        abort();
    }

    auto stringclass = env->FindClass("java/lang/String");
    auto args = env->NewObjectArray(argc-1, stringclass, nullptr);

    int index = 0;
    for (int i = 1; i < orig_argc; i++) {
        if (!argv[i])
            continue;
        env->SetObjectArrayElement(args, index++, env->NewStringUTF(argv[i]));
    }

    env->CallStaticVoidMethod(mainclass, mainmethod, args);

    // DestroyJavaVM() waits for all all non-daemon threads to end, and
    // only then destroys the JVM.
    jvm->DetachCurrentThread();
    jvm->DestroyJavaVM();
    return 0;
}
