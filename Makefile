# Makefile for XMMS2
#
# A fulhacked buildsystem for XMMS2. Uses a lot of
# foreach hacks in gmake. Don't even think about using
# a less capable (read: fulhackenabled) make program.
#

MAJOR = 1
MINOR = 9
PATCHLEVEL = 5
EXTRA = -bk
NAME = Teh korv

%:: SCCS/s.%

# Include the configuration
include config

CFLAGS += -Isrc/include -DPKGLIBDIR=\"$(PKGLIBDIR)\" -D$(OS) -DHAVE_SQLITE
LINKFLAGS += -Wl,--export-dynamic

makefiles := $(shell find src -name Makefile)

include $(makefiles)

# Collect all programs that should be built
PROGRAMS=$(foreach m, $(MODULE), $(if $($(m)_PROGRAM),$(OUTPUT_PRG)/$($(m)_PROGRAM)))

# Collect all libraries that should be built
LIBRARIES=$(foreach m, $(MODULE), $(if $($(m)_LIBRARIES), $(OUTPUT_LIB)/$($(m)_LIBRARIES)))

# Collect all plugins that should be built
PLUGINS=$(foreach m, $(MODULE), $(if $($(m)_PLUGIN), $(OUTPUT_PLUG)/$($(m)_PLUGIN)))

# Set them as targets.
TARGETS+=$(PROGRAMS) $(LIBRARIES) $(PLUGINS)

# This produces the target program objects.
$(foreach m, $(MODULE), $(eval $(OUTPUT_PRG)/$($(m)_PROGRAM)_OBJS:=$(foreach o, $($(m)_OBJS),$($(m)_DIR)/$(o))))

# This produces the target libraries objects.
$(foreach m, $(MODULE), $(eval $(OUTPUT_LIB)/$($(m)_LIBRARIES)_OBJS:=$(foreach o, $($(m)_OBJS),$($(m)_DIR)/$(o))))

# This produces the target plugin objects.
$(foreach m, $(MODULE), $(eval $(OUTPUT_PLUG)/$($(m)_PLUGIN)_OBJS:=$(foreach o, $($(m)_OBJS),$($(m)_DIR)/$(o))))
	
# This produces the targets program dependecies.
$(foreach m, $(MODULE), $(eval $(OUTPUT_PRG)/$($(m)_PROGRAM)_DEPENDS:=$($(m)_DEPENDS)))

# This produces the targets library dependecies.
$(foreach m, $(MODULE), $(eval $(OUTPUT_LIB)/$($(m)_LIBRARIES)_DEPENDS:=$($(m)_DEPENDS)))

# This produces the targets plugins dependecies.
$(foreach m, $(MODULE), $(eval $(OUTPUT_PLUG)/$($(m)_PLUGIN)_DEPENDS:=$($(m)_DEPENDS)))

# This produces the internal linking flags.
$(foreach l, $(MODULE), $(eval $(if $($l_LIBRARIES), $(l)_INTLINK:=-l$(basename $(patsubst lib%,%,$($l_LIBRARIES))) -L$(OUTPUT_LIB))))

# This expands the targets dependencies and sets the CFLAGS
$(foreach m, $(TARGETS), $(eval $(m)_CFLAGS=$(foreach d, $($(m)_DEPENDS),$($(d)_CFLAGS))))

# This expands the targets dependencies and sets the LINKFLAGS
$(foreach m, $(TARGETS), $(eval $(m)_LINKFLAGS=$(foreach d, $($(m)_DEPENDS),$(if $(findstring $d,$(MODULE)),$($d_INTLINK)) $($(d)_LINKFLAGS))))

all: $(TARGETS) builddir

# Expands to a target: $objects dependency mapping.
$(foreach l, $(TARGETS), $(eval $(l): $($(l)_OBJS)))

# Makes a unique variable object_CFLAGS to support different CLFAGS for different objects
$(foreach l, $(TARGETS), $(foreach o, $($l_OBJS), $(eval $(o)_CFLAGS=$($l_CFLAGS))))

$(foreach l, $(TARGETS), $(eval $(l): $(foreach d, $($l_DEPENDS), $(if $(findstring $d,$(MODULE)), $(OUTPUT_LIB)/$($d_LIBRARIES)))))

$(LIBRARIES): 
	$(CC) -shared $(LINKFLAGS) $($@_LINKFLAGS) $($@_OBJS) -o $@

$(PROGRAMS): 
	$(CC) -o $@ $($@_OBJS) $(LINKFLAGS) $($@_LINKFLAGS) 

$(PLUGINS): 
	$(CC) -shared -o $@ $($@_OBJS) $(LINKFLAGS) $($@_LINKFLAGS)
	
%.o: %.c 
	$(CC) $(CFLAGS) $($@_CFLAGS) -c -o $@ $<

builddir/%:
	$(shell ./mkinstalldirs builddir $(OUTPUT_PRG) $(OUTPUT_LIB) $(OUTPUT_PLUG))

clean:
	rm -rf builddir
