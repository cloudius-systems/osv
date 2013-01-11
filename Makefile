
mode=debug

submake = build/$(mode)/Makefile

quiet = $(if $V, $1, @echo " $2"; $1)

all: $(submake)
	$(MAKE) -C $(dir $(submake)) $@

$(submake): Makefile
	mkdir -p $(dir $@)
	echo 'mode = $(mode)' > $@
	echo 'src = ../..' >> $@
	echo 'VPATH = ../..' >> $@
	echo 'include ../../build.mak' >> $@

clean:
	$(call quiet, rm -rf build/$(mode), CLEAN)
