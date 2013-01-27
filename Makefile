
mode=debug

out = build/$(mode)
submake = $(out)/Makefile

quiet = $(if $V, $1, @echo " $2"; $1)

all: $(submake)
	ant -Dmode=$(mode) -Dout=$(abspath $(out)/tests/bench) -e -f tests/bench/build.xml
	$(MAKE) -C $(dir $(submake)) $@

$(submake): Makefile
	mkdir -p $(dir $@)
	echo 'mode = $(mode)' > $@
	echo 'src = ../..' >> $@
	echo 'VPATH = ../..' >> $@
	echo 'include ../../build.mak' >> $@

clean:
	$(call quiet, rm -rf build/$(mode), CLEAN)
