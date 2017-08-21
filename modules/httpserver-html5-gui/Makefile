src = $(shell readlink -f ../..)

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

all: module
module: gui swagger-ui-lib

swagger-ui-lib: swagger-ui/.git
.PHONY: swagger-ui-lib

swagger-ui/.git:
	cd $(src) && git submodule update --init -f modules/httpserver-html5-gui/swagger-ui

gui: osv-gui/.git
	$(call quiet,make -C osv-gui)
.PHONY: gui

osv-gui/.git:
	cd $(src) && git submodule update --init -f modules/httpserver-html5-gui/osv-gui

check:
	# Test if HTTP servers serves properly static content
	cd $(src) && \
	scripts/build image=httpserver-html5-gui.fg && \
	PYTHONPATH=$(src)/scripts:$(src)/modules/httpserver-api/tests modules/httpserver-html5-gui/tests/testhttpserver.py

.PHONY: check

.SECONDARY:
