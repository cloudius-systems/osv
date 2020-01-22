# Take libz.so from the build machine and put it in the image.
libz = $(shell $(CC) -print-file-name=libz.so.1)
module:
	echo /usr/lib/libz.so.1: $(libz) > usr.manifest
