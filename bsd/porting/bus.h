/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_BSD_BUS_H
#define _OSV_BSD_BUS_H
#include "mmu.h"
#include "sync_stub.h"
#include <xen/interface/io/xenbus.h>
#include <osv/device.h>
#include <sys/_task.h>

/* typedefs needed for bus_dma.h. Have to come before it */
/*
 * Bus address and size types
 */
typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;

/*
 * Access methods for bus resources and address space.
 */
typedef uint64_t bus_space_tag_t;
typedef uint64_t bus_space_handle_t;
#include <sys/bus_dma.h>

typedef void *driver_filter_t; 
typedef void (*driver_intr_t)(void *arg);

// should be in netporting or somewhere else, but it conflicts with other
// definitions. unused seems to be quite a common name
#define __unused    __attribute__((unused))

// not used
enum intr_type {
    INTR_OSV,
    // from bsd bus.h
    INTR_TYPE_BIO = 2,
    INTR_TYPE_NET = 4,
    INTR_TYPE_MISC = 16,
    INTR_MPSAFE = 512,
};

enum intr_trigger {
	INTR_TRIGGER_CONFORM = 0,
	INTR_TRIGGER_EDGE = 1,
	INTR_TRIGGER_LEVEL = 2
};

enum intr_polarity {
	INTR_POLARITY_CONFORM = 0,
	INTR_POLARITY_HIGH = 1,
	INTR_POLARITY_LOW = 2
};

struct xenpci_softc {
    int rid_ioport;
    int rid_memory;
    int rid_irq;
    struct resource* res_memory;    /* Resource for mem range. */
    struct resource* res_irq;   /* Resource for irq range. */
    void   *intr_cookie;

    vm_paddr_t phys_next;       /* next page from mem range */
};

struct intr_config_hook {
    void    (*ich_func)(void *arg);
    void    *ich_arg;
};

typedef struct device *device_t;

enum method_ids {
    bus_device_probe = 1,
    bus_device_attach,
    bus_device_detach,
    bus_device_shutdown,
    bus_device_suspend,
    bus_device_resume,
    bus_xenbus_otherend_changed,
};

typedef int  (*devmethod)(void);

typedef struct device_method {
    unsigned int id;
    devmethod func;
} device_method_t;

typedef struct _driver {
    const char  *name;
    device_method_t *methods;
    size_t      size;
} driver_t;

#define DEVMETHOD(id, func) { (int)bus_##id, (devmethod)func }

#define DEVMETHOD_END  { 0, NULL }

#define device_get_parent(_x) NULL

static inline int
device_delete_child(device_t dev, device_t child)
{
    printf("Implement me, line %s:%d\n",__FILE__,  __LINE__);
    return NULL;
}
        
// The two function below aim just at printing device information in
// a structured manner. We will ignore it for now.
static inline int
bus_print_child_header(device_t dev, device_t child)
{
    return 0;
}

static inline int
bus_print_child_footer(device_t dev, device_t child)
{
    return 0;
}

static inline device_state_t
device_get_state(device_t dev)
{
    return (dev->state);
}

static inline void *
device_get_ivars(device_t dev)
{
    return dev->ivars;
}

#define BUS_ADD_CHILD(...) do {} while (0)

// For now we will hook up manually ourselves in our .cc pci discovery
static inline int
BUS_TEARDOWN_INTR(void *pdev, void  *dev, struct resource *irq, void *cookie)
{
    return 0;
}

static inline int
BUS_SETUP_INTR(void *pdev, void *dev, struct resource *irq, enum intr_type type,
        void *param, driver_intr_t handler, void *arg, void *cookie)
{
    return 0;
}

static inline int
bus_generic_probe(device_t dev)
{
    return 0;
}

static inline int
bus_generic_attach(device_t dev)
{
    return 0;
}

static inline int bus_generic_shutdown(void)
{
	return 0;
}

typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;

static inline bus_dma_tag_t bus_get_dma_tag(struct device *dev)
{
    // XXX: It is good as long as it is used only for the parent device
    return NULL;
}

#define BUS_SPACE_MAXADDR   0xFFFFFFFFFFFFFFFFULL

// This only delays initialization to a point where interrupts are enabled.  So
// far, the initializations only registers devices nodes, so I am skipping. If
// we ever need more complex things, it should be implemented
static inline int
config_intrhook_establish(struct intr_config_hook *hook)
{
    return 0;
}

static inline void
config_intrhook_disestablish(struct intr_config_hook *hook)
{
}


struct disk;
static inline void disk_destroy(struct disk *dp)
{
}

#define device_printf(dev, ...) debugf(__VA_ARGS__)
__BEGIN_DECLS
void device_set_ivars(device_t dev, void *ivars);
int device_get_children(device_t dev, device_t **devlistp, int *devcountp);
int device_probe_and_attach(device_t dev);
device_t device_add_child(device_t dev, const char *name, int unit);

void disk_create(struct disk *dp, int version);
__END_DECLS

// Xenbus
#include <xen/interface/io/xenbus.h>
struct xenbus_device_ivars;
#define __BUS_ACCESSOR(xenbus, var, XENBUS, ivar, type)

#define NOTIMPL  ({ printf("Implement me, line %s:%d\n",__FILE__,  __LINE__); NULL; })

#define XENBUS_OTHEREND_CHANGED(bus, state) NOTIMPL
#define XENBUS_LOCALEND_CHANGED(bus, path) NOTIMPL

#define DEVICE_RESUME(dev) NOTIMPL

__BEGIN_DECLS
void XENBUSB_ENUMERATE_TYPE(device_t dev, const char *type);
int XENBUSB_GET_OTHEREND_NODE(device_t dev, struct xenbus_device_ivars *ivars);
void XENBUSB_OTHEREND_CHANGED(device_t bus, device_t child, XenbusState newstate);
void XENBUSB_LOCALEND_CHANGED(device_t bus, device_t child, const char *state);

const char *xenbus_get_node(device_t dev);
int xenbus_get_otherend_id(device_t dev);
const char *xenbus_get_otherend_path(device_t _dev);
const char *xenbus_get_type(device_t dev);

void xenbus_set_state(device_t dev, XenbusState state);
XenbusState xenbus_get_state(device_t dev);
__END_DECLS
#endif
