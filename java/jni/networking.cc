#include <jni.h>
#include <bsd/porting/networking.h>
#include <bsd/porting/route.h>

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

/*
 * Class:     com_cloudius_net_IFConfig
 * Method:    if_up
 * Signature: (Ljava/lang/String;)V
 */
extern "C" JNIEXPORT void JNICALL Java_com_cloudius_net_IFConfig_if_1up
  (JNIEnv *env , jclass self, jstring ifname)
{
    const char * ifname_c = env->GetStringUTFChars(ifname, 0);

    int error = osv_ifup(ifname_c);
    if (error) {
        jclass cls = env->FindClass("java/io/IOException");
        env->ThrowNew(cls, "osv_ifup failed");
    }

    env->ReleaseStringUTFChars(ifname, ifname_c);
}

/*
 * Class:     com_cloudius_net_Arp
 * Method:    add
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V
 */
extern "C" JNIEXPORT void JNICALL Java_com_cloudius_net_Arp_add
  (JNIEnv *env, jclass self, jstring ifname, jstring macaddr, jstring ip)
{
    const char * ifname_c = env->GetStringUTFChars(ifname, 0);
    const char * macaddr_c = env->GetStringUTFChars(macaddr, 0);
    const char * ip_c = env->GetStringUTFChars(ip, 0);

    osv_route_arp_add(ifname_c, ip_c, macaddr_c);

    env->ReleaseStringUTFChars(ifname, ifname_c);
    env->ReleaseStringUTFChars(macaddr, macaddr_c);
    env->ReleaseStringUTFChars(ip, ip_c);
}

/*
 * Class:     com_cloudius_net_Route
 * Method:    add_default
 * Signature: (Ljava/lang/String;)V
 */
extern "C" JNIEXPORT void JNICALL Java_com_cloudius_net_Route_add_1default
  (JNIEnv *env, jclass self, jstring gw)
{
    const char * gw_c = env->GetStringUTFChars(gw, 0);

    osv_route_add_network("0.0.0.0", "0.0.0.0", gw_c);

    env->ReleaseStringUTFChars(gw, gw_c);
}

extern "C" void dhcp_start(bool wait);

/*
 * Class:     com_cloudius_net_DHCP
 * Method:    dhcp_start
 * Signature: ()V
 */
extern "C" JNIEXPORT void JNICALL Java_com_cloudius_net_DHCP_dhcp_1start
  (JNIEnv *, jclass)
{
    dhcp_start(true);
}

