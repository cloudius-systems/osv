qemu-system-x86_64 -enable-kvm -m 1G -chardev stdio,mux=on,id=stdio -mon chardev=stdio,mode=readline,default -device isa-serial,chardev=stdio -kernel build/debug/loader.bin
