#ifndef ___FENV_H
#define ___FENV_H

#ifdef __cplusplus
extern "C" {
#endif

int feenableexcept(int);
int fedisableexcept(int);
int fegetexcept(void);

#ifdef __cplusplus
}
#endif
#endif
