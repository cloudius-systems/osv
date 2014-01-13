
mode=release

out = build/$(mode)
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

X86_64=$(shell echo __x86_64__ | $(CC) -E -xc - | tail -n 1)
ifneq ($(X86_64),1)
$(error OSv can only be built on a 64-bit x86 Linux distribution.)
endif

all: $(submake) $(modulemk)
	$(call quiet, $(silentant) ant -Dmode=$(mode) -Dout=$(abspath $(out)/tests/bench) \
		-e -f tests/bench/build.xml $(if $V,,-q), ANT tests/bench)
	$(call only-if, $(mgmt), cd mgmt && ./gradlew --daemon :web:jar build)
	$(MAKE) -r -C $(dir $(submake)) $@

$(submake) $(modulemk): Makefile
	mkdir -p $(dir $@)
	echo 'mode = $(mode)' > $@
	echo 'src = $(abspath .)' >> $@
	echo 'out = $(abspath $(out))' >> $@
	echo 'VPATH = $(abspath .)' >> $@
	echo 'include $(abspath build.mk)' >> $@

clean:
	$(call quiet, rm -rf build/$(mode), CLEAN)
	$(call only-if, $(mgmt), $(call quiet, cd mgmt && ./gradlew --daemon clean >> /dev/null, GRADLE CLEAN))

check: all
	./scripts/test.py

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
