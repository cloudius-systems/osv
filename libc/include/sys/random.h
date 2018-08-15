#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H

#ifdef __cplusplus
extern "C" {
#endif

ssize_t getrandom(void *buf, size_t count, unsigned int flags);

#ifdef __cplusplus
}
#endif
#endif
