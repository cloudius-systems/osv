#include <jni.h>
#include <bsd/porting/networking.h>

/*
 * Class:     com_cloudius_net_IFConfig
 * Method:    set_ip
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V
 */
extern "C" JNIEXPORT void JNICALL Java_com_cloudius_net_IFConfig_set_1ip
  (JNIEnv * env, jclass self, jstring ifname, jstring ip, jstring netmask)
{
    const char * ifname_c = env->GetStringUTFChars(ifname, 0);
    const char * ip_c = env->GetStringUTFChars(ip, 0);
    const char * netmask_c = env->GetStringUTFChars(netmask, 0);

    int error = osv_start_if(ifname_c, ip_c, netmask_c);
    if (error) {
        jclass cls = env->FindClass("java/io/IOException");
        env->ThrowNew(cls, "osv_start_if failed");
    }

    env->ReleaseStringUTFChars(ifname, ifname_c);
    env->ReleaseStringUTFChars(ip, ip_c);
    env->ReleaseStringUTFChars(netmask, netmask_c);
}
