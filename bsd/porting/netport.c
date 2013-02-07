#include <porting/netport.h>
#include <memory.h>

int copyin(const void *uaddr, void *kaddr, size_t len)
{
    memcpy(kaddr, uaddr, len);
    return (0);
}

int copyout(const void *kaddr, void *uaddr, size_t len)
{
    memcpy(uaddr, kaddr, len);
    return (0);
}

int copystr(const void *kfaddr, void *kdaddr, size_t len, size_t *done)
{
    // FIXME: implement
    return (0);
}

int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
{
    // FIXME: implement
    return (0);
}
