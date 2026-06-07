#
# Copyright (C) 2019 Waldemar Kozaczuk, Ltd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
SRC = $(shell readlink -f ../..)

module:
	# Support OpenSSL 3.x (libssl.so.3) and legacy 1.1.x (libssl.so.1.1).
	# OpenSSL 3.x is the current standard on modern distros and NixOS.
	if $(SRC)/scripts/manifest_from_host.sh -l libssl.so.3 > usr.manifest 2>/dev/null && \
	   grep -q libssl usr.manifest 2>/dev/null; then \
		true; \
	else \
		$(SRC)/scripts/manifest_from_host.sh -l libssl.so.1.1 > usr.manifest; \
	fi
	# Optional runtime dependencies -- silently ignored if absent on host
	$(SRC)/scripts/manifest_from_host.sh -li libgssapi_krb5.so.2 >> usr.manifest
	$(SRC)/scripts/manifest_from_host.sh -li libselinux.so.1 >> usr.manifest
	$(SRC)/scripts/manifest_from_host.sh -li liblzma.so.5 >> usr.manifest

.PHONY: module

clean:
	rm -rf usr.manifest
.PHONY: clean
