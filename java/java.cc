#include "elf.hh"
#include <jni.h>
#include <string.h>
#include "debug.hh"

// java.so is similar to the standard "java" command line launcher in Linux.
//
// This program does very little - basically it starts the JVM and asks it
// to run a fixed class, /java/RunJava.class, which parses the command line
// parameters, sets up the class path, and runs the jar or class specified
// in these parameters. Unfortunately, we cannot do this here in C++ code
// because FindClass() has a known bug where it cannot find a class inside a
// .jar, just a class in a directory.

extern elf::program* prog;

#define JVM_PATH        "/usr/lib/jvm/jre/lib/amd64/server/libjvm.so"
#define RUNJAVA_DIR     "/java"
#define RUNJAVA         "RunJava"

JavaVMOption mkoption(const char* s)
{
    JavaVMOption opt;
    opt.optionString = strdup(s);
    return opt;
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
    options.push_back(mkoption("-Djava.class.path=" RUNJAVA_DIR));
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
        debug("java.so: Can't find class %s in %s.\n", RUNJAVA, RUNJAVA_DIR);
        abort();
    }

    auto mainmethod = env->GetStaticMethodID(mainclass, "main", "([Ljava/lang/String;)V");
    if (!mainmethod) {
        debug("java.so: Can't find main() in class %s.\n", RUNJAVA);
        abort();
    }

    auto stringclass = env->FindClass("java/lang/String");
    auto args = env->NewObjectArray(argc, stringclass, nullptr);
    for (int i = 0; i < argc; ++i) {
        env->SetObjectArrayElement(args, i, env->NewStringUTF(argv[i]));
    }

    env->CallStaticVoidMethod(mainclass, mainmethod, args);
    return 0;
}
