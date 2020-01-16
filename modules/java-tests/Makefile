SRC = $(shell readlink -f ../..)
include $(SRC)/modules/java-base/common.gmk

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := obj/java_isolated.so
endif

obj/java_isolated.o: $(SRC)/modules/java-base/java.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -o $@ -c $(SRC)/modules/java-base/java.cc -MMD, CXX $@)

obj/java_isolated.so: obj/java_isolated.o $(java-base-path)/obj/jvm/java_api.o $(java-base-path)/obj/jvm/jni_helpers.o
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $^, LINK $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)

module: $(java-targets)
	cd $(SRC)/modules/java-tests && mvn -q package -DskipTests=true

clean:
	cd $(SRC)/modules/java-tests && mvn -q clean
	-rm -f dependency-reduced-pom.xml
	$(call very-quiet, $(RM) -rf obj)
