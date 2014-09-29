#
# Copyright (C) 2014 Cloudius Systems, Ltd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#

module: certs build/cloud-init.yaml
.PHONY: module

clean:
	rm -rf build/*
.PHONY: clean

build:
	mkdir -p build

.PHONY: certs
certs: build/.certs-tag

build/.certs-tag: build
	umask 077 && openssl genrsa 2048 > build/cakey.pem
	certtool --generate-self-signed --load-privkey build/cakey.pem \
		--template ca.info --outfile build/cacert.pem 2> /dev/null

	# We must get unique serial numbers, which are time-based by default !
	sleep 1

	umask 077 && openssl genrsa 2048 > build/server.key
	certtool --generate-certificate --load-privkey build/server.key \
		--load-ca-certificate build/cacert.pem --load-ca-privkey build/cakey.pem \
		--template server.info --outfile build/server.pem 2> /dev/null

	sleep 1

	umask 077 &&  openssl genrsa 2048 > build/client.key
	certtool --generate-certificate --load-privkey build/client.key \
		--load-ca-certificate build/cacert.pem --load-ca-privkey build/cakey.pem \
		--template client.info --outfile build/client.pem 2> /dev/null

	touch build/.certs-tag

build/cloud-init.yaml: build/.certs-tag Makefile
	echo "httpserver:" > $@
	echo "   ssl: yes" >> $@
	echo "files:" >> $@
	echo "   /etc/pki/server.pem: |" >> $@
	awk '{ print "         "$$0; }' build/server.pem >> $@
	echo "   /etc/pki/private/server.key: |" >> $@
	awk '{ print "         "$$0; }' build/server.key >> $@
	echo "   /etc/pki/CA/cacert.pem: |" >> $@
	awk '{ print "         "$$0; }' build/cacert.pem >> $@
