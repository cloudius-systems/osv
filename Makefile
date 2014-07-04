
mode=release
ARCH := $(subst x86_64,x64,$(shell uname -m))

outlink = build/$(mode)
out = build/$(mode).$(ARCH)
submake = $(out)/Makefile
modulemk = $(out)/module/module.mk

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

all: $(submake) $(modulemk)
	$(call quiet, $(silentant) ant -Dmode=$(mode) -Dout=$(abspath $(out)/tests/bench) \
		-e -f tests/bench/build.xml $(if $V,,-q), ANT tests/bench)
	$(call quiet, $(silentant) ant -Dmode=$(mode) -Dout=$(abspath $(out)/tests/reclaim) \
		-e -f tests/reclaim/build.xml $(if $V,,-q), ANT tests/reclaim)
	$(MAKE) -r -C $(dir $(submake)) $@

$(submake) $(modulemk): Makefile prepare-dir
	mkdir -p $(dir $@)
	echo 'mode = $(mode)' > $@
	echo 'src = $(abspath .)' >> $@
	echo 'out = $(abspath $(out))' >> $@
	echo 'VPATH = $(abspath .)' >> $@
	echo 'include $(abspath build.mk)' >> $@

.PHONY: prepare-dir

prepare-dir:
	# transition from build/release being the output directory
	# to build/release being a symlink to build/release.x64
	[ ! -e $(out) -o -L $(outlink) ] || rm -rf $(outlink)
	mkdir -p $(out)
	ln -nsf $(notdir $(out)) $(outlink)

clean-core:
	$(call quiet, rm -rf $(outlink) $(out), CLEAN)
	$(call quiet, cd java && mvn clean -q, MVN CLEAN)
.PHONY: clean-core

clean: clean-core
	$(call quiet, OSV_BASE=. MAKEFLAGS= scripts/module.py clean -q, MODULES CLEAN)
.PHONY: clean

check: export image ?= tests

check: all
	./scripts/test.py

osv.vmdk osv.vdi:
	$(MAKE) -r -C $(dir $(submake)) $@
.PHONY: osv.vmdk osv.vdi

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
