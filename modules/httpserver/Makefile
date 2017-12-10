# This "make clean" should not be needed because we no longer build anything
# in this directory, but prior to commit c446634 we did create things here,
# and if we forget to delete them, especially usr.manifest, future builds
# may fail (see issue #931).
clean:
	-rm -rf autogen obj
	-rm -f libhttpserver*.so usr.manifest
module:
.PHONY: clean module
