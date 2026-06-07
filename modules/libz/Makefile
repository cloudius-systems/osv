# Take libz.so from the build machine and put it in the image.
very-quiet = $(if $V, $1, @$1)
libz_cc = $(shell $(CC) -print-file-name=libz.so.1)
# On NixOS, CC -print-file-name returns a relative path; fall back to LD_LIBRARY_PATH search.
libz = $(if $(filter /%,$(libz_cc)),$(libz_cc),$(firstword $(shell \
	for d in $$(echo "$$LD_LIBRARY_PATH" | tr ':' ' '); do \
		f=$$(find "$$d" -maxdepth 1 -name 'libz.so.1' 2>/dev/null | head -1); \
		test -n "$$f" && echo "$$f" && break; \
	done)))
module:
	$(call very-quiet, echo /usr/lib/libz.so.1: $(libz) > usr.manifest)
