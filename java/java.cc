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

extern "C" int main(int ac, char **av)
{
    prog->add_object(JVM_PATH);

    auto JNI_GetDefaultJavaVMInitArgs
        = prog->lookup_function<void (void*)>("JNI_GetDefaultJavaVMInitArgs");
    JavaVMInitArgs vm_args = {};
    vm_args.version = JNI_VERSION_1_6;
    JNI_GetDefaultJavaVMInitArgs(&vm_args);
    std::vector<JavaVMOption> options;
    options.push_back(mkoption("-Djava.class.path=/java"));
    while (ac > 0 && av[0][0] == '-' && av[0] != std::string("-jar")) {
        options.push_back(mkoption(av[0]));
        ++av, --ac;
    }
    if (ac < 1) {
        abort();
    }
    std::string mainclassname;
    if (std::string(av[0]) == "-jar") {
        mainclassname = "RunJar";
    } else {
        mainclassname = av[0];
    }
    ++av, --ac;
    vm_args.nOptions = options.size();
    vm_args.options = options.data();

    auto JNI_CreateJavaVM
        = prog->lookup_function<jint (JavaVM**, JNIEnv**, void*)>("JNI_CreateJavaVM");
    JavaVM* jvm = nullptr;
    JNIEnv *env;

    auto ret = JNI_CreateJavaVM(&jvm, &env, &vm_args);
    assert(ret == 0);
    auto mainclass = env->FindClass(mainclassname.c_str());

    auto mainmethod = env->GetStaticMethodID(mainclass, "main", "([Ljava/lang/String;)V");
    auto stringclass = env->FindClass("java/lang/String");
    std::vector<std::string> newargs;
    for (auto i = 0; i < ac; ++i) {
        newargs.push_back(av[i]);
    }

    auto args = env->NewObjectArray(newargs.size(), stringclass, nullptr);
    for (auto i = 0u; i < newargs.size(); ++i) {
        env->SetObjectArrayElement(args, i, env->NewStringUTF(newargs[i].c_str()));
    }
    env->CallStaticVoidMethod(mainclass, mainmethod, args);
    return 0;
}

