qemu-system-x86_64 \
	-vnc :1 \
	-enable-kvm \
	-cpu host,+x2apic \
	-m 1G \
	-chardev stdio,mux=on,id=stdio \
	-mon chardev=stdio,mode=readline,default \
	-device isa-serial,chardev=stdio \
	-device virtio-net-pci \
	-drive file=build/debug/loader.img,if=virtio,cache=unsafe
