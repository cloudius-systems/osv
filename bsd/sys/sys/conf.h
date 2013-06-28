#ifndef _OSV_BSD_SYS_CONF_H
#define _OSV_BSD_SYS_CONF_H
#if CONF_logger_debug
  #define bootverbose 1
#else
  #define bootverbose 0
#endif
#endif
