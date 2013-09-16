
mode=release

out = build/$(mode)
submake = $(out)/Makefile

quiet = $(if $V, $1, @echo " $2"; $1)
silentant = $(if $V,, scripts/silentant.py)

# It's not practical to build large Java programs from make, because of
# how Java does dependencies; so we use ant instead.  But we also cannot
# call ant from the main makefile (build.mk), since make will have no
# idea whether the target has changed or not.  So we call ant from here,
# and then the main makefile can treat the build products (jars) as inputs

all: $(submake)
	$(call quiet, $(silentant) ant -Dmode=$(mode) -Dout=$(abspath $(out)/tests/bench) \
		-e -f tests/bench/build.xml $(if $V,,-q), ANT tests/bench)
	$(call quiet, cd mgmt && ./gradlew :web:jar build >> /dev/null, GRADLE :web:jar build)
	$(MAKE) -C $(dir $(submake)) $@

$(submake): Makefile
	mkdir -p $(dir $@)
	echo 'mode = $(mode)' > $@
	echo 'src = ../..' >> $@
	echo 'VPATH = ../..' >> $@
	echo 'include ../../build.mk' >> $@

qcow2: all
	qemu-img convert -f raw -O qcow2 build/$(mode)/usr.img build/$(mode)/usr.qcow2

clean:
	$(call quiet, rm -rf build/$(mode), CLEAN)
	$(call quiet, cd mgmt && ./gradlew clean >> /dev/null , GRADLE CLEAN)
	

external:
	cd external/libunwind && autoreconf -i
	cd external/libunwind && sh config.sh
	make -C external/libunwind
	cp external/libunwind/src/.libs/libunwind.a .
	make -C external/glibc-testsuite

.PHONY: external
.DELETE_ON_ERROR:
