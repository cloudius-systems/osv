SRC = $(shell readlink -f ../..)

include $(SRC)/modules/java-base/common.gmk

ifeq ($(arch),aarch64)
java-targets :=
else
java-targets := obj/java.so
endif

jar-targets := $(java-base-path)/runjava-isolated/target/runjava-isolated.jar

obj/java.o: $(java-base-path)/java.cc | init
	$(call quiet, $(CXX) $(CXXFLAGS) -o $@ -c $(java-base-path)/java.cc -MMD, CXX $@)

obj/java.so: obj/java.o $(java-base-path)/obj/jvm/java_api.o $(java-base-path)/obj/jvm/jni_helpers.o $(java-base-path)/obj/balloon/jvm_balloon.o
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $^, LINK $@)

init:
	@echo "  MKDIRS"
	$(call very-quiet, mkdir -p obj)

comma := ,

$(jar-targets): $(wildcard $(java-base-path)/runjava-isolated/src/main/java/io/osv/isolated/*.java) \
		$(wildcard $(java-base-path)/runjava-isolated/src/main/java/io/osv/util/*.java) \
		$(wildcard $(java-base-path)/runjava-isolated/src/main/java/io/osv/jul/*.java) \
		$(wildcard $(java-base-path)/runjava-common/src/main/java/io/osv/*.java) \
		$(wildcard $(java-base-path)/runjava-common/src/main/java/io/osv/util/*.java) \
		$(java-base-path)/pom.xml $(java-base-path)/runjava-common/pom.xml \
		$(java-base-path)/runjava-isolated/pom.xml
	$(call quiet, cd $(java-base-path) && mvn -q --projects :runjava-common$(comma):runjava-isolated package -DskipTests=true, MVN $@)

module: $(java-targets) $(jar-targets)

clean:
	cd $(java-base-path) && mvn -q clean
	-rm -f dependency-reduced-pom.xml
	$(call very-quiet, $(RM) -rf obj)
