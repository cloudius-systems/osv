SRC = $(shell readlink -f ../..)

include $(SRC)/modules/java-base/common.gmk

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := obj/java_non_isolated.so
endif

jar-targets := $(java-base-path)/runjava-non-isolated/target/runjava-non-isolated.jar

obj/java_non_isolated.o: $(java-base-path)/java.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -DRUN_JAVA_NON_ISOLATED -o $@ -c $(java-base-path)/java.cc -MMD, CXX $@)

obj/java_non_isolated.so: obj/java_non_isolated.o $(java-base-path)/obj/jvm/java_api.o \
		$(java-base-path)/obj/jvm/jni_helpers.o $(java-base-path)/obj/balloon/jvm_balloon.o
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $^, LINK $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)

comma := ,

$(jar-targets): $(wildcard $(java-base-path)/runjava-non-isolated/src/main/java/io/osv/nonisolated/*.java) \
		$(wildcard $(java-base-path)/runjava-common/src/main/java/io/osv/*.java) \
		$(wildcard $(java-base-path)/runjava-common/src/main/java/io/osv/util/*.java) \
		$(java-base-path)/pom.xml $(java-base-path)/runjava-common/pom.xml \
		$(java-base-path)/runjava-non-isolated/pom.xml
	$(call quiet, cd $(java-base-path) && mvn -q --projects :runjava-common$(comma):runjava-non-isolated package -DskipTests=true, MVN $@)

module: $(java-targets) $(jar-targets)

clean:
	cd $(java-base-path) && mvn -q clean
	-rm -f dependency-reduced-pom.xml
	$(call very-quiet, $(RM) -rf obj)
