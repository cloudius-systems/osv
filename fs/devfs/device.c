/*-
 * Copyright (c) 2005-2009, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * device.c - device I/O support routines
 */

/**
 * The device_* system calls are interfaces to access the specific
 * device object which is handled by the related device driver.
 *
 * The routines in this moduile have the following role:
 *  - Manage the name space for device objects.
 *  - Forward user I/O requests to the drivers with minimum check.
 *  - Provide the table for the Driver-Kernel Interface.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include <osv/prex.h>
#include <osv/mutex.h>
#include <osv/device.h>
#include <osv/debug.h>
#include <osv/buf.h>

#include <geom/geom_disk.h>

struct mutex sched_mutex = MUTEX_INITIALIZER;

#define sched_lock()	mutex_lock(&sched_mutex);
#define sched_unlock()	mutex_unlock(&sched_mutex);


/* list head of the devices */
static struct device *device_list = NULL;

/*
 * Look up a device object by device name.
 */
static struct device *
device_lookup(const char *name)
{
	struct device *dev;

	for (dev = device_list; dev != NULL; dev = dev->next) {
		if (!strncmp(dev->name, name, MAXDEVNAME))
			return dev;
	}
	return NULL;
}

struct partition_table_entry {
	uint8_t  bootable;
	uint8_t  starting_head;
	uint16_t starting_sector:6;
	uint16_t starting_cylinder:10;
	uint8_t  system_id;
	uint8_t  ending_head;
	uint16_t ending_sector:6;
	uint16_t ending_cylinder:10;
	uint32_t rela_sector;
	uint32_t total_sectors;
} __attribute__((packed));

/*
 * read_partition_table - given a device @dev, create one subdevice per partition
 * found in that device.
 *
 * This function will read a partition table from the canonical location of the
 * device pointed by @dev. For each partition found, a new device will be
 * created. The newly created device will have most of its data copied from
 * @dev, except for its name, offset and size.
 */
void read_partition_table(struct device *dev)
{
	struct buf *bp;
	unsigned long offset;
	int index;

	bread(dev, 0, &bp);

	sched_lock();
	for (offset = 0x1be, index = 0; offset < 0x1fe; offset += 0x10, index++) {
		struct partition_table_entry *entry;
		char dev_name[MAXDEVNAME];
		struct device *new_dev;

		entry = bp->b_data + offset;

		if (entry->system_id == 0) {
			continue;
		}

		if (entry->starting_sector == 0) {
			continue;
		}

		snprintf(dev_name, MAXDEVNAME, "%s.%d", dev->name, index);
		new_dev = device_create(dev->driver, dev_name, dev->flags);
		free(new_dev->private_data);

		new_dev->offset = (off_t)entry->rela_sector << 9;
		new_dev->size = (off_t)entry->total_sectors << 9;
		new_dev->max_io_size = dev->max_io_size;
		new_dev->private_data = dev->private_data;
		device_set_softc(new_dev, device_get_softc(dev));
	}

	sched_unlock();
	brelse(bp);
}

void device_register(struct device *dev, const char *name, int flags)
{
	size_t len;
	void *private = NULL;

	assert(dev->driver != NULL);

	/* Check the length of name. */
	len = strnlen(name, MAXDEVNAME);
	if (len == 0 || len >= MAXDEVNAME)
		return;

	sched_lock();

	/* Check if specified name is already used. */
	if (device_lookup(name) != NULL)
		sys_panic("duplicate device");

	/*
	 * Allocate a device and device private data.
	 */
	if (dev->driver->devsz != 0) {
		if ((private = malloc(dev->driver->devsz)) == NULL)
			sys_panic("devsz");
		memset(private, 0, dev->driver->devsz);
	}

	strlcpy(dev->name, name, len + 1);
	dev->flags = flags;
	dev->active = 1;
	dev->refcnt = 1;
	dev->offset = 0;
	dev->private_data = private;
	dev->next = device_list;
	dev->max_io_size = UINT_MAX;
	device_list = dev;

	sched_unlock();
}


/*
 * device_create - create new device object.
 *
 * A device object is created by the device driver to provide
 * I/O services to applications.  Returns device ID on
 * success, or 0 on failure.
 */
struct device *
device_create(struct driver *drv, const char *name, int flags)
{
	struct device *dev;
	size_t len;

	assert(drv != NULL);

	/* Check the length of name. */
	len = strnlen(name, MAXDEVNAME);
	if (len == 0 || len >= MAXDEVNAME)
		return NULL;

	/*
	 * Allocate a device structure.
	 */
	if ((dev = malloc(sizeof(*dev))) == NULL)
		sys_panic("device_create");

    dev->driver = drv;
    device_register(dev, name, flags);
	return dev;
}

#if 0
int
device_destroy(struct device *dev)
{

	sched_lock();
	if (!device_valid(dev)) {
		sched_unlock();
		return ENODEV;
	}
	dev->active = 0;
	device_release(dev);
	sched_unlock();
	return 0;
}
#endif

#if 0
/*
 * Return device's private data.
 */
static void *
device_private(struct device *dev)
{
	assert(dev != NULL);
	assert(dev->private_data != NULL);

	return dev->private_data;
}
#endif

/*
 * Return true if specified device is valid.
 */
static int
device_valid(struct device *dev)
{
	struct device *tmp;
	int found = 0;

	for (tmp = device_list; tmp != NULL; tmp = tmp->next) {
		if (tmp == dev) {
			found = 1;
			break;
		}
	}
	if (found && dev->active)
		return 1;
	return 0;
}

/*
 * Increment the reference count on an active device.
 */
static int
device_reference(struct device *dev)
{

	sched_lock();
	if (!device_valid(dev)) {
		sched_unlock();
		return ENODEV;
	}
	dev->refcnt++;
	sched_unlock();
	return 0;
}

/*
 * Decrement the reference count on a device. If the reference
 * count becomes zero, we can release the resource for the device.
 */
static void
device_release(struct device *dev)
{
	struct device **tmp;

	sched_lock();
	if (--dev->refcnt > 0) {
		sched_unlock();
		return;
	}
	/*
	 * No more references - we can remove the device.
	 */
	for (tmp = &device_list; *tmp; tmp = &(*tmp)->next) {
		if (*tmp == dev) {
			*tmp = dev->next;
			break;
		}
	}
	free(dev);
	sched_unlock();
}

/*
 * device_open - open the specified device.
 *
 * Even if the target driver does not have an open
 * routine, this function does not return an error. By
 * using this mechanism, an application can check whether
 * the specific device exists or not. The open mode
 * should be handled by an each device driver if it is
 * needed.
 */
int
device_open(const char *name, int mode, struct device **devp)
{
	struct devops *ops;
	struct device *dev;
	int error;

	sched_lock();
	if ((dev = device_lookup(name)) == NULL) {
		sched_unlock();
		return ENXIO;
	}
	error = device_reference(dev);
	if (error) {
		sched_unlock();
		return error;
	}
	sched_unlock();

	ops = dev->driver->devops;
	assert(ops->open != NULL);
	error = (*ops->open)(dev, mode);
	*devp = dev;

	device_release(dev);
	return error;
}

/*
 * device_close - close a device.
 *
 * Even if the target driver does not have close routine,
 * this function does not return any errors.
 */
int
device_close(struct device *dev)
{
	struct devops *ops;
	int error;

	if ((error = device_reference(dev)) != 0)
		return error;

	ops = dev->driver->devops;
	assert(ops->close != NULL);
	error = (*ops->close)(dev);

	device_release(dev);
	return error;
}

int
device_read(struct device *dev, struct uio *uio, int ioflags)
{
	struct devops *ops;
	int error;

	if ((error = device_reference(dev)) != 0)
		return error;

	ops = dev->driver->devops;
	assert(ops->read != NULL);
	error = (*ops->read)(dev, uio, ioflags);

	device_release(dev);
	return error;
}

int
device_write(struct device *dev, struct uio *uio, int ioflags)
{
	struct devops *ops;
	int error;

	if ((error = device_reference(dev)) != 0)
		return error;

	ops = dev->driver->devops;
	assert(ops->write != NULL);
	error = (*ops->write)(dev, uio, ioflags);

	device_release(dev);
	return error;
}

/*
 * device_ioctl - I/O control request.
 *
 * A command and its argument are completely device dependent.
 * The ioctl routine of each driver must validate the user buffer
 * pointed by the arg value.
 */
int
device_ioctl(struct device *dev, u_long cmd, void *arg)
{
	struct devops *ops;
	int error;

	if ((error = device_reference(dev)) != 0)
		return error;

	ops = dev->driver->devops;
	assert(ops->ioctl != NULL);
	error = (*ops->ioctl)(dev, cmd, arg);

	device_release(dev);
	return error;
}

#if 0
/*
 * Device control - devctl is similar to ioctl, but is invoked from
 * other device driver rather than from user application.
 */
static int
device_control(struct device *dev, u_long cmd, void *arg)
{
	struct devops *ops;
	int error;

	assert(dev != NULL);

	sched_lock();
	ops = dev->driver->devops;
	assert(ops->devctl != NULL);
	error = (*ops->devctl)(dev, cmd, arg);
	sched_unlock();
	return error;
}

/*
 * device_broadcast - broadcast devctl command to all device objects.
 *
 * If "force" argument is true, we will continue command
 * notification even if some driver returns an error. In this
 * case, this routine returns EIO error if at least one driver
 * returns an error.
 *
 * If force argument is false, a kernel stops the command processing
 * when at least one driver returns an error. In this case,
 * device_broadcast will return the error code which is returned
 * by the driver.
 */
static int
device_broadcast(u_long cmd, void *arg, int force)
{
	struct device *dev;
	struct devops *ops;
	int error, retval = 0;

	sched_lock();

	for (dev = device_list; dev != NULL; dev = dev->next) {
		/*
		 * Call driver's devctl() routine.
		 */
		ops = dev->driver->devops;
		if (ops == NULL)
			continue;

		assert(ops->devctl != NULL);
		error = (*ops->devctl)(dev, cmd, arg);
		if (error) {
			if (force)
				retval = EIO;
			else {
				retval = error;
				break;
			}
		}
	}
	sched_unlock();
	return retval;
}
#endif

/*
 * Return device information.
 */
int
device_info(struct devinfo *info)
{
	u_long target = info->cookie;
	u_long i = 0;
	struct device *dev;
	int error = ESRCH;

	sched_lock();
	for (dev = device_list; dev != NULL; dev = dev->next) {
		if (i++ == target) {
			info->cookie = i;
			info->id = dev;
			info->flags = dev->flags;
			strlcpy(info->name, dev->name, MAXDEVNAME);
			error = 0;
			break;
		}
	}
	sched_unlock();
	return error;
}

int
enodev(void)
{
	return ENODEV;
}

int
nullop(void)
{
	return 0;
}
