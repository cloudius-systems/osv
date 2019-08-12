SRC = $(shell readlink -f ../..)

include $(SRC)/modules/java-base/common.gmk

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := obj/java_non_isolated.so
endif

obj/java_non_isolated.o: $(SRC)/modules/java-base/java.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -DRUN_JAVA_NON_ISOLATED -o $@ -c $(SRC)/modules/java-base/java.cc -MMD, CXX $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)

module: $(java-targets)
	cd $(SRC)/modules/java-base && mvn -q --projects :runjava-common,:runjava-non-isolated package -DskipTests=true

clean:
	cd $(SRC)/modules/java-base && mvn -q clean
	-rm -f dependency-reduced-pom.xml
	$(call very-quiet, $(RM) -rf obj)
