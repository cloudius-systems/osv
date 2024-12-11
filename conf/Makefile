mode=release
ifeq (,$(wildcard conf/$(mode).mk))
    $(error unsupported mode $(mode))
endif

HOST_CXX := g++

detect_arch = $(word 1, $(shell { echo "x64        __x86_64__";  \
                                  echo "aarch64    __aarch64__"; \
                       } | $1 -E -xc - | grep ' 1$$'))

host_arch := $(call detect_arch, $(HOST_CXX))

# As an alternative to setting ARCH or arch, let's allow the user to
# directly set the CROSS_PREFIX environment variable, and learn its arch:
ifdef CROSS_PREFIX
    ARCH := $(call detect_arch, $(CROSS_PREFIX)gcc)
endif

ifndef ARCH
    ARCH := $(host_arch)
endif
arch := $(ARCH)

ifeq (,$(wildcard conf/$(arch).mk))
    $(error unsupported architecture $(arch))
endif

quiet = $(if $V, $1, @echo " $2"; $1)

out = build/$(mode).$(arch)
default_config : $(out)/.config

.PHONY: override_dot_file
ifneq (,$(conf_dot_file))
    ifeq (,$(wildcard $(conf_dot_file)))
        $(error Missing configuration file $(conf_dot_file))
    endif
override_dot_file:
	$(call quiet, mkdir -p $(out), MKDIR $(out))
	$(call quiet, rsync --checksum $(conf_dot_file) $(out)/.config, RSYNC $(conf_dot_file) TO $(out)/.config)
endif

.PHONY: config
config: override_dot_file $(out)/gen/config/kernel_conf.mk $(out)/default_version_script

$(out)/default_version_script: exported_symbols/*.symbols exported_symbols/$(arch)/*.symbols
	$(call quiet, scripts/generate_version_script.sh $(out)/default_version_script, GEN default_version_script)

#Build conf and mconf binaries if not built yet
$(out)/kbuild/kconfig/conf $(out)/kbuild/kconfig/mconf: $(wildcard kbuild/kconfig/*.c)
	$(call quiet, mkdir -p $(out)/kbuild, MKDIR $(out)/kbuild)
	$(call quiet, cd $(out)/kbuild && make -s -C ../../../kbuild -f Makefile.osv O=`pwd` -j, MAKE kbuild)

CONF_FILES := conf/base.mk conf/$(mode).mk conf/$(arch).mk conf/profiles/$(arch)/kconfig $(wildcard conf/kconfig/*)

#Generate the default .config, gen/config/kernel.conf, and gen/include/osv/kernel_config.h if not generated yet
$(out)/.config: $(CONF_FILES) $(out)/kbuild/kconfig/conf
	$(call quiet, mode=$(mode) arch=$(arch) CONFIG_=CONF_ KCONFIG_AUTOHEADER=$(out)/gen/include/osv/kernel_config.h KCONFIG_AUTOCONFIG=$(out)/gen/config/kernel.conf KCONFIG_RUSTCCFG=$(out)/gen/include/osv/rustc_cfg KCONFIG_CONFIG=$(out)/.config $(out)/kbuild/kconfig/conf -s conf/kconfig/main --alldefconfig, CONF_DEF $(out)/.config)

#Generate the .config.yes, gen/include/osv/kernel_yes_config.h with all available options
$(out)/.config.yes: $(CONF_FILES) $(out)/kbuild/kconfig/conf
	$(call quiet, mode=$(mode) arch=$(arch) CONFIG_=CONF_ KCONFIG_AUTOHEADER=$(out)/gen/include/osv/kernel_yes_config.h KCONFIG_AUTOCONFIG=$(out)/gen/config_yes/kernel_yes.conf KCONFIG_RUSTCCFG=$(out)/gen/include/osv/rustc_cfg KCONFIG_CONFIG=$(out)/.config.yes $(out)/kbuild/kconfig/conf -s conf/kconfig/main --allyesconfig, CONF_YES $(out)/.config.yes)

#Synchronize gen/include/osv/kernel_config.h and gen/config/kernel.conf with .config if the latter has changed maybe by mconf
$(out)/gen/include/osv/kernel_config.h: $(out)/.config
	$(call quiet, mode=$(mode) arch=$(arch) CONFIG_=CONF_ KCONFIG_AUTOHEADER=$(out)/gen/include/osv/kernel_config.h KCONFIG_AUTOCONFIG=$(out)/gen/config/kernel.conf KCONFIG_RUSTCCFG=$(out)/gen/include/osv/rustc_cfg KCONFIG_CONFIG=$(out)/.config $(out)/kbuild/kconfig/conf -s conf/kconfig/main --syncconfig, SYNC $(out)/.config)

#Generate gen/config/kernel_conf.mk AND individual option headers gen/include/osv/kernel_config_* based on the latest gen/config/kernel.conf and gen/include/osv/kernel_config.h
#The gen/config/kernel_conf.mk is included by the main OSv makefile and the headers gen/include/osv/kernel_config_* included by relevant source files
$(out)/gen/config/kernel_conf.mk: $(out)/gen/include/osv/kernel_config.h $(out)/.config.yes
	$(call quiet, sed 's/CONF/conf/' $(out)/gen/config/kernel.conf | sed 's/=y$$/=1/' | sed 's/\(conf_drivers_profile\)_\(.*\)=1/\1=\2/' > $(out)/gen/config/kernel_conf.mk, CONF_MK $(out)/gen/config/kernel_conf.mk)
	$(call quiet, scripts/gen-kernel-config-headers $(out)/gen/include/osv/kernel_config.h $(out)/gen/include/osv/kernel_yes_config.h, CONF_HEADERS $(out)/gen/include/osv/kernel_config_*)

#Start mconf to allow user modify config options, that will be saved in updated .config if any changed
menuconfig:
	mode=$(mode) arch=$(arch) CONFIG_=CONF_ KCONFIG_AUTOHEADER=$(out)/gen/include/osv/kernel_config.h KCONFIG_AUTOCONFIG=$(out)/gen/config/kernel.conf KCONFIG_RUSTCCFG=$(out)/gen/include/osv/rustc_cfg KCONFIG_CONFIG=$(out)/.config $(out)/kbuild/kconfig/mconf conf/kconfig/main
