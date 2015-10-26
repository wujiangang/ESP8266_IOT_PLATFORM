
# Directory the Makefile is in. Please don't include other Makefiles before this.
THISDIR:=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

#Include httpd config from lower level, if it exists
-include ../esphttpdconfig.mk


#Default options. If you want to change them, please create ../esphttpdconfig.mk with the options you want in it.
GZIP_COMPRESSION ?= no
COMPRESS_W_YUI ?= no
YUI-COMPRESSOR ?= /usr/bin/yui-compressor
USE_HEATSHRINK ?= yes



# Output directors to store intermediate compiled files
# relative to the project directory
BUILD_BASE	= build

# Base directory for the compiler. Needs a / at the end; if not set it'll use the tools that are in
# the PATH.
XTENSA_TOOLS_ROOT ?= 

# base directory of the ESP8266 SDK package, absolute
#SDK_BASE	?= /opt/Espressif/ESP8266_SDK
SDK_BASE = $(SDK_PATH)
HTMLDIR = ../html_plug
# name for the target project
LIB		= libesphttpd.a libwebpages-espfs.a

# which modules (subdirectories) of the project to include in compiling
MODULES		= espfs core util
EXTRA_INCDIR	= ./include \
					. \
					lib/heatshrink/


# compiler flags using during compilation of source files
CFLAGS		= -Os -ggdb -std=c99 -Werror -Wpointer-arith -Wundef -Wall -Wl,-EL -fno-inline-functions \
		-nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH \
		-Wno-address -ffunction-sections -fdata-sections

# various paths from the SDK used in this project
SDK_LIBDIR	= lib
SDK_LDDIR	= ld

SDK_INCDIR	= include
SDK_ESP_INCDIR	= include/espressif
SDK_OS_INCDIR	= include/freertos
SDK_JSON_INCDIR	= include/json
SDK_LWIP_INCDIR	= include/lwip
SDK_LWIP4_INCDIR = include/lwip/ipv4
SDK_LWIP6_INCDIR = include/lwip/ipv6
SDK_EXTRA_INCDIR = extra_include
SDK_XTENSA_INCDIR = extra_include/xtensa

# select which tools to use as compiler, librarian and linker
CC		:= $(XTENSA_TOOLS_ROOT)xtensa-lx106-elf-gcc
AR		:= $(XTENSA_TOOLS_ROOT)xtensa-lx106-elf-ar
LD		:= $(XTENSA_TOOLS_ROOT)xtensa-lx106-elf-gcc
OBJCOPY	:= $(XTENSA_TOOLS_ROOT)xtensa-lx106-elf-objcopy

####
#### no user configurable options below here
####
SRC_DIR		:= $(MODULES)
BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(MODULES))

SDK_INCDIR	:= $(addprefix -I$(SDK_BASE)/,$(SDK_INCDIR))
SDK_INCDIR	+= $(addprefix -I$(SDK_BASE)/,$(SDK_ESP_INCDIR))
SDK_INCDIR  += $(addprefix -I$(SDK_BASE)/,$(SDK_OS_INCDIR))
SDK_INCDIR  += $(addprefix -I$(SDK_BASE)/,$(SDK_JSON_INCDIR))
SDK_INCDIR  += $(addprefix -I$(SDK_BASE)/,$(SDK_LWIP_INCDIR))
SDK_INCDIR  += $(addprefix -I$(SDK_BASE)/,$(SDK_LWIP4_INCDIR))
SDK_INCDIR  += $(addprefix -I$(SDK_BASE)/,$(SDK_LWIP6_INCDIR))
SDK_INCDIR  += $(addprefix -I$(SDK_BASE)/,$(SDK_EXTRA_INCDIR))
SDK_INCDIR  += $(addprefix -I$(SDK_BASE)/,$(SDK_XTENSA_INCDIR))


SRC		:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ		:= $(patsubst %.c,$(BUILD_BASE)/%.o,$(SRC))

INCDIR	:= $(addprefix -I,$(SRC_DIR))
EXTRA_INCDIR	:= $(addprefix -I,$(EXTRA_INCDIR))
MODULE_INCDIR	:= $(addsuffix /include,$(INCDIR))

V ?= $(VERBOSE)
ifeq ("$(V)","1")
Q :=
vecho := @true
else
Q := @
vecho := @echo
endif

ifeq ("$(GZIP_COMPRESSION)","yes")
CFLAGS		+= -DGZIP_COMPRESSION
endif

ifeq ("$(USE_HEATSHRINK)","yes")
CFLAGS		+= -DESPFS_HEATSHRINK
endif

vpath %.c $(SRC_DIR)

define compile-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(INCDIR) $(MODULE_INCDIR) $(EXTRA_INCDIR) $(SDK_INCDIR) $(CFLAGS)  -c $$< -o $$@
endef

.PHONY: all checkdirs clean webpages.espfs submodules

all: checkdirs $(LIB) webpages.espfs

submodules: lib/heatshrink/Makefile
lib/heatshrink/Makefile:
	$(Q) echo "Heatshrink isn't found. Checking out submodules to fetch it."
	$(Q) git submodule init
	$(Q) git submodule update


$(LIB): $(BUILD_DIR) submodules $(OBJ)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $(OBJ)

checkdirs: $(BUILD_DIR)

$(BUILD_DIR):
	$(Q) mkdir -p $@


webpages.espfs: $(HTMLDIR) espfs/mkespfsimage/mkespfsimage
ifeq ("$(COMPRESS_W_YUI)","yes")
	$(Q) rm -rf html_compressed;
	$(Q) cp -r ../html html_compressed;
	$(Q) echo "Compression assets with yui-compressor. This may take a while..."
	$(Q) for file in `find html_compressed -type f -name "*.js"`; do $(YUI-COMPRESSOR) --type js $$file -o $$file; done
	$(Q) for file in `find html_compressed -type f -name "*.css"`; do $(YUI-COMPRESSOR) --type css $$file -o $$file; done
	$(Q) awk "BEGIN {printf \"YUI compression ratio was: %.2f%%\\n\", (`du -b -s html_compressed/ | sed 's/\([0-9]*\).*/\1/'`/`du -b -s ../html/ | sed 's/\([0-9]*\).*/\1/'`)*100}"
# mkespfsimage will compress html, css and js files with gzip by default if enabled
# override with -g cmdline parameter
	$(Q) cd html_compressed; find . | $(THISDIR)/espfs/mkespfsimage/mkespfsimage > $(THISDIR)/webpages.espfs; cd ..;
else
	$(Q) cd $(HTMLDIR); find . | $(THISDIR)/espfs/mkespfsimage/mkespfsimage > $(THISDIR)/webpages.espfs; cd ..
endif

libwebpages-espfs.a: webpages.espfs
	$(Q) $(OBJCOPY) -I binary -O elf32-xtensa-le -B xtensa --rename-section .data=.irom0.literal \
		--redefine-sym _binary_webpages_espfs_start=webpages_espfs_start \
		--redefine-sym _binary_webpages_espfs_end=webpages_espfs_end \
		--redefine-sym _binary_webpages_espfs_size=webpages_espfs_size \
		webpages.espfs build/webpages.espfs.o
	$(Q) $(AR) cru $@ build/webpages.espfs.o

espfs/mkespfsimage/mkespfsimage: espfs/mkespfsimage/
	$(Q) $(MAKE) -C espfs/mkespfsimage USE_HEATSHRINK="$(USE_HEATSHRINK)" GZIP_COMPRESSION="$(GZIP_COMPRESSION)"

clean:
	$(Q) rm -f $(LIB)
	$(Q) find $(BUILD_BASE) -type f | xargs rm -f
	$(Q) make -C espfs/mkespfsimage/ clean
	$(Q) rm -rf $(FW_BASE)
	$(Q) rm -f webpages.espfs libwebpages-espfs.a
ifeq ("$(COMPRESS_W_YUI)","yes")
	$(Q) rm -rf html_compressed
endif

$(foreach bdir,$(BUILD_DIR),$(eval $(call compile-objects,$(bdir))))
