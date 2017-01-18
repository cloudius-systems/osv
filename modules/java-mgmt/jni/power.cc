/* To compile this into a shared object:
 * cc -s -fPIC -shared -I/usr/lib/jvm/java/include -I/usr/lib/jvm/java/include/linux -o stty.so stty.c
 */

#include <osv/power.hh>

#include <jni.h>

extern "C" {
JNIEXPORT void JNICALL
Java_com_cloudius_util_Power_reboot
(JNIEnv *env, jobject self)
{
    osv::reboot();
}
}
