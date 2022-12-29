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

javac_with_version = $(shell javac -version)

module:
	@case "$(javac_with_version)" in \
		"javac 9"*) ;; \
		"javac 1"[0-9]*) ;; \
		*) echo "Requires Java 9 or later"; exit 1 ;; \
	esac
	$(call very-quiet, if [ "$(libsunec_path)" != "" ]; then $(SRC)/scripts/manifest_from_host.sh $(libsunec_path) > usr.manifest; fi)
	$(call very-quiet, $(SRC)/scripts/manifest_from_host.sh -li libfreeblpriv3.so >> usr.manifest)
	$(call very-quiet, $(SRC)/scripts/manifest_from_host.sh -li libnss3.so >> usr.manifest)
	$(call very-quiet, echo "/usr/lib/jvm/java/lib/security/default.policy: $(java_jdk_path)/lib/security/default.policy" >> usr.manifest)
	$(call very-quiet, echo "/usr/lib/jvm/java/conf/**: $(java_jdk_path)/conf/**" >> usr.manifest)

clean:
	rm -f usr.manifest
