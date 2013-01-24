#include "elf.hh"
#include <jni.h>
#include <string.h>

extern elf::program* prog;

#define JVM_PATH        "/usr/lib/jvm/jre/lib/amd64/server/libjvm.so"

int main(int ac, char **av)
{
    prog->add_object(JVM_PATH);

    auto JNI_GetDefaultJavaVMInitArgs
        = prog->lookup_function<void (void*)>("JNI_GetDefaultJavaVMInitArgs");
    JavaVMInitArgs vm_args = {};
    vm_args.version = JNI_VERSION_1_6;
    JNI_GetDefaultJavaVMInitArgs(&vm_args);
    vm_args.nOptions = 1;
    vm_args.options = new JavaVMOption[1];
    vm_args.options[0].optionString = strdup("-Djava.class.path=/tests");

    auto JNI_CreateJavaVM
        = prog->lookup_function<jint (JavaVM**, JNIEnv**, void*)>("JNI_CreateJavaVM");
    JavaVM* jvm = nullptr;
    JNIEnv *env;

    auto ret = JNI_CreateJavaVM(&jvm, &env, &vm_args);
    assert(ret == 0);
    auto mainclass = env->FindClass("Hello");
    auto mainmethod = env->GetStaticMethodID(mainclass, "main", "([Ljava/lang/String;)V");
    env->CallStaticVoidMethod(mainclass, mainmethod, nullptr);
}

