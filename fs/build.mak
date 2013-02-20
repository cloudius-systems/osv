
fs :=

fs +=	fs.o

fs +=	vfs/main.o \
	vfs/kern_physio.o \
	vfs/subr_uio.o \
	vfs/vfs_bdev.o \
	vfs/vfs_bio.o \
	vfs/vfs_conf.o \
	vfs/vfs_lookup.o \
	vfs/vfs_mount.o \
	vfs/vfs_vnode.o \
	vfs/vfs_task.o \
	vfs/vfs_syscalls.o

fs +=	ramfs/ramfs_vfsops.o \
	ramfs/ramfs_vnops.o

fs +=	devfs/devfs_vnops.o \
	devfs/device.o
fs +=	fatfs/fatfs_fat.o \
	fatfs/fatfs_node.o \
	fatfs/fatfs_subr.o \
	fatfs/fatfs_vfsops.o \
	fatfs/fatfs_vnops.o
