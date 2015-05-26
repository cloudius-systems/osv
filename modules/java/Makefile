# TODO: need to move compilation of $(java-targets) from the main makefile
# to here. Unfortunately, compiling with OSv header files is a big mess,
# and much easier to do it in the main OSv makefile :-(
SRC = $(shell readlink -f ../..)
module:
	cd $(SRC)/java && mvn package -DskipTests=true

clean:
	cd $(SRC)/java && mvn clean
	-rm -f dependency-reduced-pom.xml
