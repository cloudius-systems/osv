#ifndef _SYS_SYSMACROS_H
#define _SYS_SYSMACROS_H

extern unsigned int gnu_dev_major (unsigned long long int __dev);
extern unsigned int gnu_dev_minor (unsigned long long int __dev);
extern unsigned long long int gnu_dev_makedev (unsigned int __major,
                                               unsigned int __minor);

#define major(x) gnu_dev_major(x)
#define minor(x) gnu_dev_minor(x)
#define makedev(x,y) gnu_dev_makedev(x,y)

#endif
