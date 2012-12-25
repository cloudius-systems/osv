qemu-system-x86_64 -m 1G -chardev stdio,mux=on,id=stdio -mon chardev=stdio,mode=readline,default -device isa-serial,chardev=stdio -kernel loader.bin
