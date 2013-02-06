/*-
 * Copyright (c) 2005-2008, Kohsuke Ohtani
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

#ifndef _DEVICE_H
#define _DEVICE_H

#include <sys/cdefs.h>
#include <sys/types.h>

#include <osv/uio.h>

__BEGIN_DECLS

#define MAXDEVNAME	12
#define DO_RWMASK	0x3

struct bio;
struct device;

/*
 * Device information
 */
struct devinfo {
	u_long		cookie;		/* index cookie */
	struct device	*id;		/* device id */
	int		flags;		/* device characteristics flags */
	char		name[MAXDEVNAME]; /* device name */
};

/*
 * Device flags
 */
#define D_CHR		0x00000001	/* character device */
#define D_BLK		0x00000002	/* block device */
#define D_REM		0x00000004	/* removable device */
#define D_TTY		0x00000010	/* tty device */

typedef int (*devop_open_t)   (struct device *, int);
typedef int (*devop_close_t)  (struct device *);
typedef int (*devop_read_t)   (struct device *, struct uio *, int);
typedef int (*devop_write_t)  (struct device *, struct uio *, int);
typedef int (*devop_ioctl_t)  (struct device *, u_long, void *);
typedef int (*devop_devctl_t) (struct device *, u_long, void *);
typedef void (*devop_strategy_t)(struct bio *);

/*
 * Device operations
 */
struct devops {
	devop_open_t	open;
	devop_close_t	close;
	devop_read_t	read;
	devop_write_t	write;
	devop_ioctl_t	ioctl;
	devop_devctl_t	devctl;
	devop_strategy_t strategy;
};


#define	no_open		((devop_open_t)nullop)
#define	no_close	((devop_close_t)nullop)
#define	no_read		((devop_read_t)enodev)
#define	no_write	((devop_write_t)enodev)
#define	no_ioctl	((devop_ioctl_t)enodev)
#define	no_devctl	((devop_devctl_t)nullop)

/*
 * Driver object
 */
struct driver {
	const char	*name;		/* name of device driver */
	struct devops	*devops;	/* device operations */
	size_t		devsz;		/* size of private data */
	int		flags;		/* state of driver */
};

/*
 * flags for the driver.
 */
#define	DS_INACTIVE	0x00		/* driver is inactive */
#define DS_ALIVE	0x01		/* probe succeded */
#define DS_ACTIVE	0x02		/* intialized */
#define DS_DEBUG	0x04		/* debug */

/*
 * Device object
 */
struct device {
	struct device	*next;		/* linkage on list of all devices */
	struct driver	*driver;	/* pointer to the driver object */
	char		name[MAXDEVNAME]; /* name of device */
	int		flags;		/* D_* flags defined above */
	int		active;		/* device has not been destroyed */
	int		refcnt;		/* reference count */
	void		*private_data;	/* private storage */
};

int	 device_open(const char *, int, struct device **);
int	 device_close(struct device *);
int	 device_read(struct device *, struct uio *, int);
int	 device_write(struct device *, struct uio *, int);
int	 device_ioctl(struct device *, u_long, void *);
int	 device_info(struct devinfo *);

int	enodev(void);
int	nullop(void);

int	physio(struct device *dev, struct uio *uio, int ioflags);

struct device *	device_create(struct driver *drv, const char *name, int flags);

__END_DECLS

#endif /* !_DEVICE_H */
