export conf_drivers_xen?=0
export conf_drivers_hyperv?=0

export conf_drivers_virtio_blk?=0
ifeq ($(conf_drivers_virtio_blk),1)
export conf_drivers_virtio?=1
endif

export conf_drivers_virtio_scsi?=0
ifeq ($(conf_drivers_virtio_scsi),1)
export conf_drivers_virtio?=1
export conf_drivers_scsi?=1
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

export conf_drivers_ahci?=0
ifeq ($(conf_drivers_ahci),1)
export conf_drivers_pci?=1
endif

export conf_drivers_pvscsi?=0
ifeq ($(conf_drivers_pvscsi),1)
export conf_drivers_pci?=1
export conf_drivers_scsi?=1
endif

export conf_drivers_nvme?=0
ifeq ($(conf_drivers_nvme),1)
export conf_drivers_pci?=1
endif

export conf_drivers_vmxnet3?=0
ifeq ($(conf_drivers_vmxnet3),1)
export conf_drivers_pci?=1
endif

export conf_drivers_ena?=0
ifeq ($(conf_drivers_ena),1)
export conf_drivers_pci?=1
endif

export conf_drivers_ide?=0
ifeq ($(conf_drivers_ide),1)
export conf_drivers_pci?=1
endif

export conf_drivers_vga?=0
ifeq ($(conf_drivers_vga),1)
export conf_drivers_pci?=1
endif

export conf_drivers_pvpanic?=0
ifeq ($(conf_drivers_pvpanic),1)
export conf_drivers_acpi?=1
endif

export conf_drivers_hpet?=0
ifeq ($(conf_drivers_hpet),1)
export conf_drivers_acpi?=1
endif

export conf_drivers_acpi?=0
ifeq ($(conf_drivers_acpi),1)
export conf_drivers_pci?=1
endif

export conf_drivers_virtio?=0
export conf_drivers_pci?=0
export conf_drivers_mmio?=0
export conf_drivers_scsi?=0
