#include <osv/debug.hh>
#include <osv/sched.hh>
#include <osv/run.hh>
#include <jni.h>
#include <stdlib.h>
#include <string.h>


bool run_elf(int argc, char** argv, int *return_code)
{
    if (argc <= 0) {
        return (false);
    }


    debug("run_elf(): running main() in the context of thread %p\n",
        sched::thread::current());
    int rc;
    auto obj = osv::run(argv[0], argc, argv, &rc);
    if (!obj) {
        return false;
    }
    cancel_this_thread_alarm();

    /* set the return code */
    if (return_code) {
        *return_code = rc;
    }

    return (true);
}

static jint throwIOException(JNIEnv *env, const char *message)
{
    jclass cls = env->FindClass("java/io/IOException");
    // TODO: do something if !cls ?
    return env->ThrowNew(cls, message);
}

extern "C" JNIEXPORT jint JNICALL Java_com_cloudius_util_Exec_run
  (JNIEnv *env, jclass self, jobjectArray jargv)
{
    int argc = env->GetArrayLength(jargv);
    if (argc <= 0) {
        throwIOException(env, "no command given");
    }

    char **argv = (char**) malloc(argc * sizeof(char*));

    for (int i=0; i<argc; i++) {
        jstring string = (jstring)env->GetObjectArrayElement(jargv, i);
        const char *c_utf = env->GetStringUTFChars(string, 0);
        argv[i] = strdup(c_utf);
        env->ReleaseStringUTFChars(string, c_utf);
    }

    int rc;
    bool success = run_elf(argc, argv, &rc);

    // free argv
    for (int i=0; i<argc; i++) {
        free(argv[i]);
    }
    free(argv);

    if (!success) {
        throwIOException(env, "couldn't run command");
    }

    return rc;
}
