# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CXXFLAGS  = -g -Wall -std=c++11 -fPIC

# the build target executable:
TARGET = httpserver
CPP_FILES := $(wildcard *.cc)
OBJ_FILES := $(addprefix obj/,$(CPP_FILES:.cc=.o))

LIBS = -lpthread -lboost_system-mt -lboost_filesystem-mt -lboost_program_options

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

all: init $(TARGET) $(TARGET).so

init:
	$(call very-quiet, mkdir -p obj)

$(TARGET): $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -o $(TARGET) $^ $(LIBS), LINK $@)

$(TARGET).so: $(OBJ_FILES)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $(TARGET).so $^ $(LIBS), LINK $@)

obj/%.o: %.cc
	$(call quiet, $(CXX) $(CXXFLAGS) -c -o $@ $<, CXX $@)

clean:
	$(call quiet, $(RM) $(TARGET), CLEAN)
	$(call very-quiet, $(RM) $(TARGET).so)
	$(call very-quiet, $(RM) -rf obj/*)
