SRC = $(shell readlink -f ../..)

include $(SRC)/modules/java-base/common.gmk

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := obj/java.so
endif

obj/java.o: $(SRC)/modules/java-base/java.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -o $@ -c $(SRC)/modules/java-base/java.cc -MMD, CXX $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)

module: $(java-targets)
	cd $(SRC)/modules/java-base && mvn --projects :runjava-common,:runjava-isolated  package -DskipTests=true

clean:
	cd $(SRC)/modules/java-base && mvn clean
	-rm -f dependency-reduced-pom.xml
	$(call very-quiet, $(RM) -rf obj)
