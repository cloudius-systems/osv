#ifndef INCLUDED_EXECINFO_H
#define INCLUDED_EXECINFO_H

#ifdef __cplusplus
extern "C" {
#endif

int backtrace (void **buffer, int size);

#ifdef __cplusplus
}
#endif

#endif
