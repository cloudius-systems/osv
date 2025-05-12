export conf_drivers_xen?=0

export conf_drivers_virtio_blk?=0
ifeq ($(conf_drivers_virtio_blk),1)
export conf_drivers_virtio?=1
endif

export conf_drivers_virtio_fs?=0
ifeq ($(conf_drivers_virtio_fs),1)
export conf_drivers_virtio?=1
endif

export conf_drivers_virtio_net?=0
ifeq ($(conf_drivers_virtio_net),1)
export conf_drivers_virtio?=1
endif

export conf_drivers_virtio_rng?=0
ifeq ($(conf_drivers_virtio_rng),1)
export conf_drivers_virtio?=1
endif

export conf_drivers_nvme?=0
ifeq ($(conf_drivers_nvme),1)
export conf_drivers_pci?=1
endif

export conf_drivers_cadence?=0
export conf_drivers_virtio?=0
export conf_drivers_pci?=0
export conf_drivers_mmio?=0
