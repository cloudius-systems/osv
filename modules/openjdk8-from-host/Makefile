.PHONY: module clean

SRC = $(shell readlink -f ../..)

javac_exe_path = $(shell realpath $$(which javac))
javac_bin_path = $(shell dirname $(javac_exe_path))
java_jdk_path = $(shell dirname $(javac_bin_path))

module:
	$(SRC)/scripts/manifest_from_host.sh $(java_jdk_path)/jre/lib/amd64/libsunec.so > usr.manifest
	$(SRC)/scripts/manifest_from_host.sh -l libfreeblpriv3.so >> usr.manifest

clean:
	rm -rf usr.manifest
