/* To compile this into a shared object:
 * cc -s -fPIC -shared -I/usr/lib/jvm/java/include -I/usr/lib/jvm/java/include/linux -o stty.so stty.c
 */

#include <termios.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include <jni.h>

JNIEXPORT jlong JNICALL
Java_com_cloudius_util_Stty_saveState
(JNIEnv *env, jobject self)
{
	/* we (ab)use a Java "long" to hold a pointer, so let's check it
	 * actually fits... */
	if (sizeof(jlong) < sizeof (struct termios *))
		abort();

	struct termios *t = malloc(sizeof(struct termios));
	if (!t)
		return 0;
	ioctl(0, TCGETS, t);
	return (jlong) t;
}

JNIEXPORT void JNICALL
Java_com_cloudius_util_Stty_freeState
(JNIEnv *env, jobject self, jlong addr)
{
	if (!addr)
		return;
	free((struct termios *)addr);
}

JNIEXPORT void JNICALL
Java_com_cloudius_util_Stty_raw
(JNIEnv *env, jobject self)
{
	struct termios t;
	ioctl(0, TCGETS, &t);
	/* TODO: Maybe it's just simpler to set the flags to some fixed
	 * value, instead of trying to modify them like this.
	 */
	t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | IGNCR | INLCR |
			ICRNL | IXON);
	t.c_oflag &= ~OPOST;
	t.c_lflag &= ~(ICANON | ECHO | ECHONL | ISIG | IEXTEN);
	t.c_cflag &= ~(CSIZE | PARENB);
	t.c_cflag |= CS8;
	ioctl(0, TCSETS, &t);
}

JNIEXPORT void JNICALL
Java_com_cloudius_util_Stty_reset
(JNIEnv *env, jobject self, jlong addr)
{
	if (!addr)
		return;
	ioctl(0, TCSETS, (struct termios *)addr);
}
