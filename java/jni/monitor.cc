#include <jni.h>
#include "monitor.hh"
#include "java/jvm_balloon.hh"

/*
 * Class:     io_osv_OSvGCMonitor
 * Method:    NotifyOSv
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_io_osv_OSvGCMonitor_NotifyOSv(JNIEnv *env, jclass mon, jlong handle)
{
    jvm_balloon_shrinker *shrinker = (jvm_balloon_shrinker *)handle;
    shrinker->release_memory(0);
    shrinker->activate_shrinker();
}
