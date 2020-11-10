SRC = $(shell readlink -f ../..)
include $(SRC)/modules/java-base/common.gmk

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := obj/java_isolated.so
endif

isolated-jar-target := $(java-base-path)/runjava-isolated/target/runjava-isolated.jar

tests-jar-target := tests/target/runjava-tests.jar

tests-isolates-target := tests-isolates/target/tests-isolates-jar-with-dependencies.jar

tests-jre-extension-target := tests-jre-extension/target/tests-jre-extension.jar

obj/java_isolated.o: $(SRC)/modules/java-base/java.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -o $@ -c $(SRC)/modules/java-base/java.cc -MMD, CXX $@)

obj/java_isolated.so: obj/java_isolated.o $(java-base-path)/obj/jvm/java_api.o $(java-base-path)/obj/jvm/jni_helpers.o $(java-base-path)/obj/balloon/jvm_balloon.o
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $^, LINK $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)

comma := ,

$(isolated-jar-target): $(wildcard $(java-base-path)/runjava-isolated/src/main/java/io/osv/isolated/*.java) \
		$(wildcard $(java-base-path)/runjava-isolated/src/main/java/io/osv/util/*.java) \
		$(wildcard $(java-base-path)/runjava-isolated/src/main/java/io/osv/jul/*.java) \
		$(wildcard $(java-base-path)/runjava-common/src/main/java/io/osv/*.java) \
		$(wildcard $(java-base-path)/runjava-common/src/main/java/io/osv/util/*.java) \
		$(java-base-path)/pom.xml $(java-base-path)/runjava-common/pom.xml \
		$(java-base-path)/runjava-isolated/pom.xml
	$(call quiet, cd $(java-base-path) && mvn -q --projects :runjava-common$(comma):runjava-isolated package -DskipTests=true, MVN $@)

$(tests-jre-extension-target): $(wildcard tests-jre-extension/src/main/java/tests/*.java) \
		tests-jre-extension/pom.xml pom.xml
	$(call quiet, cd $(SRC)/modules/java-tests && mvn -q --projects :tests-jre-extension package -DskipTests=true, MVN $@)

$(tests-isolates-target): $(isolated-jar-target) $(wildcard tests-isolates/src/main/java/tests/*.java) \
		pom.xml tests-isolates/pom.xml
	$(call quiet, cd $(SRC)/modules/java-tests && \
	mvn -q --projects :runjava-common$(comma):runjava-non-isolated$(comma):runjava-isolated$(comma):tests-isolates package -DskipTests=true, MVN $@)

$(tests-jar-target): $(isolated-jar-target) $(tests-isolates-target) $(tests-jre-extension-target) \
		$(wildcard tests/src/main/java/io/osv/*.java) \
		$(wildcard tests/src/main/java/tests/*.java) \
		pom.xml tests/pom.xml
	$(call quiet, cd $(SRC)/modules/java-tests && \
	mvn -q --projects :runjava-common$(comma):runjava-non-isolated$(comma):runjava-isolated$(comma):tests-isolates$(comma):runjava-tests$(comma):tests-jre-extension package -DskipTests=true, MVN $@)

module: $(java-targets) $(tests-jar-target)

clean:
	cd $(SRC)/modules/java-tests && mvn -q clean
	-rm -f dependency-reduced-pom.xml
	$(call very-quiet, $(RM) -rf obj)
