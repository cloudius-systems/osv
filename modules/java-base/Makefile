include ../common.gmk

module_out := $(out)/modules/java-base
export module_out

include common.gmk
include $(out)/gen/config/kernel_conf.mk

ifeq ($(conf_memory_jvm_balloon),1)
java-targets := $(module_out)/jni/monitor.so $(module_out)/jvm/jni_helpers.o $(module_out)/jvm/java_api.o $(module_out)/balloon/jvm_balloon.o
else
java-targets := $(module_out)/jni/monitor.so $(module_out)/jvm/jni_helpers.o $(module_out)/jvm/java_api.o
endif

module: all

all: $(init) $(java-targets)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p $(module_out)/jni)
	$(call very-quiet, mkdir -p $(module_out)/jvm)
	$(call very-quiet, mkdir -p $(module_out)/balloon)
.PHONY: init

clean:
	$(call very-quiet, $(RM) -rf $(module_out))
