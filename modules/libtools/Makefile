include ../common.gmk

module_out := $(out)/modules/libtools

COMMON += -O2 -fPIC -D __BSD_VISIBLE=1 -D _KERNEL=1
CXXFLAGS = -std=gnu++11 $(COMMON)
CFLAGS = $(COMMON)
quiet = $(if $V, $1, @echo " $2"; $1)

module: $(module_out)/libtools.so $(module_out)/lsroute.so $(module_out)/ifconfig.so
	@echo "/usr/lib/libtools.so: $(module_out)/libtools.so" > usr.manifest;

$(module_out)/libtools.so: $(module_out)/route_info.o $(module_out)/network_interface.o
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $^, LINK libtools.so)

$(module_out)/lsroute.so: $(module_out)/lsroute.o $(module_out)/libtools.so
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $^, LINK lsroute.so)

$(module_out)/ifconfig.so: $(module_out)/ifconfig.o $(module_out)/libtools.so
	$(call quiet, $(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $^, LINK ifconfig.so)

$(module_out)/%.o: %.cc
	$(call very-quiet, mkdir -p $(module_out))
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $*.cc)

clean:
	-rm -rf $(module_out) usr.manifest
