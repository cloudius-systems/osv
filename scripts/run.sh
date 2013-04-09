#!/bin/bash

argc=$#
if [ $argc -ne 1 ]; then
	echo "run.sh: default to 'release'"
	v="release"
elif [[ "$1" != "release" && "$1" != "debug" ]]; then
	echo "Usage: run.sh [debug|release]"
	exit 1
else
	v=$1
fi

qemu-system-x86_64 \
	-vnc :1 \
	-enable-kvm \
	-gdb tcp::1234,server,nowait \
	-cpu host,+x2apic \
	-m 1G \
	-smp 4 \
	-chardev stdio,mux=on,id=stdio \
	-mon chardev=stdio,mode=readline,default \
	-device isa-serial,chardev=stdio \
	-device virtio-net-pci \
	-drive file=build/$v/loader.img,if=virtio,cache=unsafe \
	-drive file=build/$v/usr.img,if=virtio,cache=unsafe
