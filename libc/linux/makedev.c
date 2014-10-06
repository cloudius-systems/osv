#include <endian.h>
#include <sys/sysmacros.h>

unsigned int
gnu_dev_major (unsigned long long int dev)
{
  return ((unsigned int)( ((dev >> 31 >> 1) & 0xfffff000)
          | ((dev >> 8) & 0x00000fff) ));
}

unsigned int
gnu_dev_minor (unsigned long long int dev)
{
  return ((unsigned int)( ((dev >> 12) & 0xffffff00)
          | (dev & 0x000000ff) ));
}

unsigned long long int
gnu_dev_makedev (unsigned int major, unsigned int minor)
{
  return (((major & 0xfffff000ULL) << 32)
          | ((major & 0x00000fffULL) << 8)
          | ((minor & 0xffffff00ULL) << 12)
          | (minor & 0x000000ffULL));
}
