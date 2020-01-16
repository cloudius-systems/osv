SRC = $(shell readlink -f ../..)

include $(SRC)/modules/java-base/common.gmk

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := obj/java_non_isolated.so
endif

obj/java_non_isolated.o: $(java-base-path)/java.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -DRUN_JAVA_NON_ISOLATED -o $@ -c $(java-base-path)/java.cc -MMD, CXX $@)

obj/java_non_isolated.so: obj/java_non_isolated.o $(java-base-path)/obj/jvm/java_api.o $(java-base-path)/obj/jvm/jni_helpers.o
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $^, LINK $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)

module: $(java-targets)
	cd $(java-base-path) && mvn -q --projects :runjava-common,:runjava-non-isolated package -DskipTests=true

clean:
	cd $(java-base-path) && mvn -q clean
	-rm -f dependency-reduced-pom.xml
	$(call very-quiet, $(RM) -rf obj)
