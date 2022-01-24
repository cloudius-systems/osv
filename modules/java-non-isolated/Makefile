include ../common.gmk

module_out := $(out)/modules/java-non-isolated
export module_out

include ../java-base/common.gmk

java-targets := $(module_out)/java_non_isolated.so

jar-targets := $(java-base-path)/runjava-non-isolated/target/runjava-non-isolated.jar

$(module_out)/java_non_isolated.o: $(java-base-path)/java.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -DRUN_JAVA_NON_ISOLATED -o $@ -c $(java-base-path)/java.cc -MMD, CXX $@)

$(module_out)/java_non_isolated.so: $(module_out)/java_non_isolated.o $(out)/modules/java-base/jvm/java_api.o \
		$(out)/modules/java-base/jvm/jni_helpers.o $(out)/modules/java-base/balloon/jvm_balloon.o
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $^, LINK $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p $(module_out))

comma := ,

$(jar-targets): $(wildcard $(java-base-path)/runjava-non-isolated/src/main/java/io/osv/nonisolated/*.java) \
		$(wildcard $(java-base-path)/runjava-common/src/main/java/io/osv/*.java) \
		$(wildcard $(java-base-path)/runjava-common/src/main/java/io/osv/util/*.java) \
		$(java-base-path)/pom.xml $(java-base-path)/runjava-common/pom.xml \
		$(java-base-path)/runjava-non-isolated/pom.xml
	$(call quiet, cd $(java-base-path) && JAVA_HOME=$(java_jdk_path) mvn -q --projects :runjava-common$(comma):runjava-non-isolated package -DskipTests=true, MVN $@)

module: $(java-targets) $(jar-targets)

clean:
	cd $(java-base-path) && mvn -q clean
	-rm -f dependency-reduced-pom.xml
	$(call very-quiet, $(RM) -rf $(module_out))
