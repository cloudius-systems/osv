#include <algorithm>
#include "elf.hh"
#include <jni.h>
#include <string.h>

extern elf::program* prog;

const int argc_max_arguments = 256;

bool run_elf(int argc, char** argv, int *return_code)
{
    if ((argc <= 0) || (argc > argc_max_arguments)) {
        return (false);
    }

    auto obj = prog->add_object(argv[0]);
    if (!obj) {
        return (false);
    }

    auto main = obj->lookup<int (int, char**)>("main");
    if (!main) {
       return (false);
    }

    /* call main in a thread */
    int rc = main(argc, argv);

    /* cleanups */
    prog->remove_object(argv[0]);

    /* set the return code */
    if (return_code) {
        *return_code = rc;
    }

    return (true);
}

/*
 * Class:     com_cloudius_util_ELFLoader
 * Method:    run
 * Signature: (Ljava/util/String;)Z
 */
extern "C" JNIEXPORT jboolean JNICALL Java_com_cloudius_util_Exec_run
  (JNIEnv *env, jclass self, jobjectArray jargv)
{
    char *argv[argc_max_arguments];

    int argc = std::min(env->GetArrayLength(jargv), argc_max_arguments);
    if (argc <= 0) {
        return (JNI_FALSE);
    }

    for (int i=0; i<argc; i++) {
        jstring string = (jstring)env->GetObjectArrayElement(jargv, i);
        const char *c_utf = env->GetStringUTFChars(string, 0);
        argv[i] = strdup(c_utf);
        env->ReleaseStringUTFChars(string, c_utf);
    }

    int rc = -1;
    bool success = run_elf(argc, argv, &rc);

    // free argv
    for (int i=0; i<argc; i++) {
        free(argv[i]);
    }

    if (!success) {
        return (JNI_FALSE);
    }

    // set the return code
    jfieldID fid = env->GetStaticFieldID(self, "_exitcode", "I");
    env->SetStaticIntField(self, fid, (int)rc);

    return (JNI_TRUE);
}
