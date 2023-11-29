include conf/profiles/$(arch)/hyperv.mk
include conf/profiles/$(arch)/vbox.mk
include conf/profiles/$(arch)/virtio-mmio.mk
include conf/profiles/$(arch)/virtio-pci.mk
include conf/profiles/$(arch)/vmware.mk
include conf/profiles/$(arch)/xen.mk
include conf/profiles/$(arch)/aws.mk

conf_drivers_vga?=1
