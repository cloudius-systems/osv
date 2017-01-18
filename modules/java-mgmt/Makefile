SRC = $(shell readlink -f ../..)

include $(SRC)/modules/java-base/common.gmk

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := obj/jni/balloon.so obj/jni/elf-loader.so obj/jni/networking.so \
	obj/jni/stty.so obj/jni/tracepoint.so obj/jni/power.so
endif

module: all

all: $(java-targets)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)
	$(call very-quiet, mkdir -p obj/jni)

clean:
	$(call very-quiet, $(RM) -rf obj)
	$(call very-quiet, $(RM) -rf obj/jni)
