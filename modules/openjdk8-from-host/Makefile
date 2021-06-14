.PHONY: module clean

include ../common.gmk
ifneq ($(arch),$(host_arch))
$(error Cannot provide JDK when cross-compiling)
endif

SRC = $(shell readlink -f ../..)

javac_exe_path = $(shell realpath $$(which javac))
javac_bin_path = $(shell dirname $(javac_exe_path))
java_jdk_path = $(shell dirname $(javac_bin_path))
libsunec_path = $(shell find $(java_jdk_path) -name libsunec.so)

module:
	$(call very-quiet, $(SRC)/scripts/manifest_from_host.sh $(libsunec_path) > usr.manifest)
	$(call very-quiet, $(SRC)/scripts/manifest_from_host.sh -li libfreeblpriv3.so >> usr.manifest)

clean:
	rm -f usr.manifest
