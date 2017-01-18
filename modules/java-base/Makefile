include common.gmk

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := obj/jni/monitor.so
endif

module: all

all: $(init) $(java-targets)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj/jni)
.PHONY: init

clean:
	$(call very-quiet, $(RM) -rf obj)
