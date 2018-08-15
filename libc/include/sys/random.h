#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H

/*
 * Flags for getrandom(2)
 *
 * GRND_NONBLOCK	Don't block and return EAGAIN instead
 * GRND_RANDOM		Use the /dev/random pool instead of /dev/urandom
 */
#define GRND_NONBLOCK	0x0001
#define GRND_RANDOM	0x0002

#ifdef __cplusplus
extern "C" {
#endif

ssize_t getrandom(void *buf, size_t count, unsigned int flags);

#ifdef __cplusplus
}
#endif
#endif
