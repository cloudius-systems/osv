INCLUDES = -I../../include
COMMON_FLAGS = -g -Wall -fPIC $(INCLUDES) -O2
CXXFLAGS = -std=c++11 $(COMMON_FLAGS)

.PHONY: module
module: sysinfo.so

quiet = $(if $V, $1, @echo " $2"; $1)

sysinfo.so: sysinfo.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ sysinfo.cc, LINK $@)

clean:
	rm -f sysinfo.so sysinfo.d
