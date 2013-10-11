
mode=release

out = build/$(mode)
submake = $(out)/Makefile

quiet = $(if $V, $1, @echo " $2"; $1)
silentant = $(if $V,, scripts/silentant.py)

# $(call only-if, value, what-to-do-if-true)
only-if = $(if $(strip $(subst 0,,$1)),$2,@\#)

mgmt = 1

# It's not practical to build large Java programs from make, because of
# how Java does dependencies; so we use ant instead.  But we also cannot
# call ant from the main makefile (build.mk), since make will have no
# idea whether the target has changed or not.  So we call ant from here,
# and then the main makefile can treat the build products (jars) as inputs

all: $(submake)
	$(call quiet, $(silentant) ant -Dmode=$(mode) -Dout=$(abspath $(out)/tests/bench) \
		-e -f tests/bench/build.xml $(if $V,,-q), ANT tests/bench)
	$(call only-if, $(mgmt), cd mgmt && ./gradlew --daemon :web:jar build)
	$(MAKE) -r -C $(dir $(submake)) $@

$(submake): Makefile
	mkdir -p $(dir $@)
	echo 'mode = $(mode)' > $@
	echo 'src = ../..' >> $@
	echo 'out = $(abspath $(out))' >> $@
	echo 'VPATH = ../..' >> $@
	echo 'include ../../build.mk' >> $@

qcow2: all
	qemu-img convert -f raw -O qcow2 build/$(mode)/usr.img build/$(mode)/usr.qcow2

clean:
	$(call quiet, rm -rf build/$(mode), CLEAN)
	$(call only-if, $(mgmt), $(call quiet, cd mgmt && ./gradlew --daemon clean >> /dev/null, GRADLE CLEAN))

external:
	cd external/libunwind && autoreconf -i
	cd external/libunwind && sh config.sh
	make -C external/libunwind
	cp external/libunwind/src/.libs/libunwind.a .
	make -C external/glibc-testsuite
.PHONY: external

# "tags" is the default output file of ctags, "TAGS" is that of etags
tags TAGS:
	rm -f -- "$@"
	find . -name "*.cc" -o -name "*.hh" -o -name "*.h" -o -name "*.c" |\
		xargs $(if $(filter $@, tags),ctags,etags) -a
.PHONY: tags TAGS

cscope:
	find -name '*.[chS]' -o -name "*.cc" -o -name "*.hh" | cscope -bq -i-
	@echo cscope index created
.PHONY: cscpoe

.DELETE_ON_ERROR:
