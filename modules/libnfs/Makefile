src = $(shell readlink -f ../..)
module-dir = $(src)/modules/libnfs

all: module
module: libnfs

libnfs: upstream/libnfs/.git upstream/libnfs/lib/libnfs.so.4.0.0

.PHONY: libnfs

upstream/libnfs/.git:
	mkdir -p $(module-dir)/upstream && cd $(module-dir)/upstream && \
	git clone --depth 1 https://github.com/sahlberg/libnfs.git

upstream/libnfs/lib/libnfs.so.4.0.0:
	cd $(module-dir)/upstream/libnfs && cmake . && make

clean:
	cd $(module-dir) && rm -rf upstream
