#include "elf.hh"
#include <jni.h>
#include <string.h>

extern elf::program* prog;

#define JVM_PATH        "/usr/lib/jvm/jre/lib/amd64/server/libjvm.so"

JavaVMOption mkoption(const char* s)
{
    JavaVMOption opt;
    opt.optionString = strdup(s);
    return opt;
}

int main(int ac, char **av)
{
    prog->add_object(JVM_PATH);

    auto JNI_GetDefaultJavaVMInitArgs
        = prog->lookup_function<void (void*)>("JNI_GetDefaultJavaVMInitArgs");
    JavaVMInitArgs vm_args = {};
    vm_args.version = JNI_VERSION_1_6;
    JNI_GetDefaultJavaVMInitArgs(&vm_args);
    std::vector<JavaVMOption> options;
    options.push_back(mkoption("-Djava.class.path=/tests"));
    while (ac > 0 && av[0][0] == '-') {
        options.push_back(mkoption(av[0]));
        ++av, --ac;
    }
    vm_args.nOptions = options.size();
    vm_args.options = options.data();

    auto JNI_CreateJavaVM
        = prog->lookup_function<jint (JavaVM**, JNIEnv**, void*)>("JNI_CreateJavaVM");
    JavaVM* jvm = nullptr;
    JNIEnv *env;

    auto ret = JNI_CreateJavaVM(&jvm, &env, &vm_args);
    assert(ret == 0);
    auto mainclass = env->FindClass(av[0]);
    ++av, --ac;

    auto mainmethod = env->GetStaticMethodID(mainclass, "main", "([Ljava/lang/String;)V");
    auto stringclass = env->FindClass("java/lang/String");
    auto args = env->NewObjectArray(ac, stringclass, nullptr);
    for (auto i = 0; i < ac; ++av) {
        env->SetObjectArrayElement(args, i, env->NewStringUTF(av[i]));
    }
    env->CallStaticVoidMethod(mainclass, mainmethod, args);
}

