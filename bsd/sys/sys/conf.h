#ifndef _OSV_BSD_SYS_CONF_H
#define _OSV_BSD_SYS_CONF_H
#include <osv/kernel_config_logger_debug.h>
#if CONF_logger_debug
  #define bootverbose 1
#else
  #define bootverbose 0
#endif
#endif
