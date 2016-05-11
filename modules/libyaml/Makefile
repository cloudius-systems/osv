# Take libyaml-cpp.so from the build machine and put it in the image.
libyaml = $(shell $(CC) -print-file-name=libyaml-cpp.so)
module:
	echo /usr/lib/`readlink $(libyaml)`: $(libyaml) > usr.manifest
