#ifndef COMPILER_H_
#define COMPILER_H_

#ifdef HAVE_ATTR_COLD_LABEL
#  define ATTR_COLD_LABEL __attribute__((cold))
#else
#  define ATTR_COLD_LABEL
#endif

#endif /* COMPILER_H_ */
