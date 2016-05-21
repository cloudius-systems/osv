# When made through scripts/build, we get $(ARCH). But we don't really
# need to rely on it - we can also see the link in build/last.
ARCH ?= $(shell expr `readlink ../../build/last` : '.*\.\(.*\)') 

OSV_BASE ?= ../..
OSV_BUILD_PATH ?= ../../build/last

src = $(OSV_BASE)
out = $(OSV_BUILD_PATH)

# Some header files from OSv are used in this code. Ideally, we would take
# from OSv only the OSv-specific header files and take the rest of the system
# header files from the build machine, but sadly, the OSv header files are
# and also this code is a complete jumble of interdepedent mess, so we
# currently need to include all the OSv stuff *instead* of the system header
# files. We also have to define a couple of bizarre variables so that the
# header files make some of the definitions available. The horror.
INCLUDES = -I$(src)/arch/$(ARCH) \
	-I$(src) -I$(src)/include \
        -I$(src)/arch/common -isystem $(src)/include/glibc-compat \
        -isystem $(src)/include/api -isystem $(src)/include/api/$(ARCH) \
        -isystem $(out)/gen/include -isystem $(src)/bsd/sys -isystem $(src)/bsd -isystem $(src)/bsd/$(ARCH) \
	 -D __BSD_VISIBLE=1 -D _KERNEL=1


COMMON = -O2 -fPIC $(INCLUDES)
CXXFLAGS = -std=gnu++11 $(COMMON)
CFLAGS = $(COMMON)
quiet = $(if $V, $1, @echo " $2"; $1)

module: libtools.so lsroute.so ifconfig.so

libtools.so: route_info.o network_interface.o
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o libtools.so $^, LINK libtools.so)

lsroute.so: lsroute.o libtools.so
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $^, LINK lsroute.so)

ifconfig.so: ifconfig.o libtools.so
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $^, LINK ifconfig.so)

clean:
	-rm -f libtools.so network_interface.o route_info.o ifconfig.o \
		ifconfig.so lsroute.o lsroute.so
