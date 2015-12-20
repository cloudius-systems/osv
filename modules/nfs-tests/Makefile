
quiet = $(if $V, $1, @echo " $2"; $1)

UNFS_URL="https://github.com/benoit-canet/unfsd.git"

module: all

all:
	$(call quiet, git clone $(UNFS_URL)) && \
	$(call quiet, cd unfsd) && \
	$(call quiet, ./configure) && \
	$(call quiet, make)
	$(call quiet, cp unfsd/unfsd unfsd.bin)

