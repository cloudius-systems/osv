# Take libz.so from the build machine and put it in the image.
very-quiet = $(if $V, $1, @$1)
libz = $(shell $(CC) -print-file-name=libz.so.1)
module:
	$(call very-quiet, echo /usr/lib/libz.so.1: $(libz) > usr.manifest)
