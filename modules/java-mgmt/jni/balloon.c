#include <stdio.h>
#include <sys/mman.h>
#include <jni.h>

/* Hint: "javah -jni com.cloudius.balloon.Balloon" can be used to generate
 * the following function signature - if you don't know how it was produced.
 */
JNIEXPORT jboolean JNICALL Java_com_cloudius_balloon_Balloon_giveup(
	JNIEnv *env, jobject self, jbyteArray array) {
    jboolean iscopy=0;
    jbyte *p = (*env)->GetPrimitiveArrayCritical(env, array, &iscopy);
    if (iscopy) {
        // Failed to get the address of the array (got a copy instead,
        // which might not be on the heap). This should never happen on
        // any known Java
        return JNI_FALSE;
    }

    int len = (*env)->GetArrayLength(env, array);
    fprintf(stderr,"Hi! %p len %d\n", p, len);

    // Java allocations aren't necessarily page-aligned. We can unmap only the
    // page-aligned part of the given byte array p,len - so we fix p,len to be
    // this part. This can waste at most 4096 bytes of the array, which is
    // negligable for typically used array sizes (e.g., 1MB).
    // TODO: think if to give an option to use hugepage size here instead of
    // small page size, and get better performance for remaining memory (the
    // munmap hole forces us to split the huge pages on its edge)
    jbyte *newp = (jbyte*) (((unsigned long)p+4095) & ~4095);
    len = (len-(newp-p)) & ~4095;
    p = newp;
    fprintf(stderr,"After page alignment: %p len %d\n", p, len);


    // TODO: Think about the huge page issue - sometimes may (?) make
    // sense not to unmap the entire range, but rather full hugepages out
    // of it?
    // TODO: probably need a special kind of munmap() which also marks the
    // released virtual addresses in some way so we'll know to handle
    // it specially (as a balloon move) when we get access to these
    // addresses?
    munmap(p, len);


    (*env)->ReleasePrimitiveArrayCritical(env, array, p, 0);
    return JNI_TRUE;
    //    jbyte *p = (*env)->GetByteArrayElements(env, array, &iscopy);
    //    (*env)->ReleaseByteArrayElements(env, array, p, 0);
    //        (*env)->DeleteGlobalRef(env, array);
}
