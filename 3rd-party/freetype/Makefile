include ../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

DIR = $(shell basename `pwd`)
LOCAL_SRC_DIR  = $(shell pwd)
LIB = $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)/lib$(DIR).a
BUILD =  $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/freetype/

SRC_DIRS :=

CFILES :=
CFILES_EXCLUDE :=
CFLAGS :=

CXXFILES :=
CXXFILES_EXCLUDE :=
CXXFLAGS :=

CFILES += freetype_wrap.c

CFILES += \
      freetype/src/base/ftsystem.c \
      freetype/src/base/ftinit.c \
      freetype/src/base/ftdebug.c \
      freetype/src/base/ftbase.c \
      freetype/src/base/ftbbox.c \
      freetype/src/base/ftglyph.c \
      freetype/src/base/ftbitmap.c \
      freetype/src/base/ftmm.c \
      # freetype/src/base/ftbdf.c \
      freetype/src/base/ftcid.c \
      freetype/src/base/ftfstype.c \
      freetype/src/base/ftgasp.c \
      freetype/src/base/ftgxval.c \
      freetype/src/base/ftotval.c \
      freetype/src/base/ftpatent.c \
      freetype/src/base/ftpfr.c \
      freetype/src/base/ftstroke.c \
      freetype/src/base/ftsynth.c \
      freetype/src/base/fttype1.c \
      freetype/src/base/ftwinfnt.c \

CFILES += \
      freetype/src/sfnt/sfnt.c \
      freetype/src/truetype/truetype.c

CFILES += \
      freetype/src/smooth/smooth.c \
      # freetype/src/autofit/autofit.c \
      freetype/src/raster/raster.c \
      freetype/src/sdf/sdf.c \

CFILES += \
      freetype/src/cache/ftcache.c

CFLAGS += -I. -Ifreetype/include
CFLAGS += -DFT2_BUILD_LIBRARY
CFLAGS += -DFT_CONFIG_MODULES_H="\"canmv_ftmodule.h\""
CFLAGS += -DFT_CONFIG_OPTIONS_H="\"canmv_ftoption.h\""

CWARN := 
# CWARN += -Wall -Werror
# CWARN += -Wno-int-conversion

CFLAGS += -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany $(CWARN)

CFILES_ALL        = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.c))
CXXFILES_ALL      = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.cpp))

CFILES      += $(filter-out $(CFILES_EXCLUDE), $(CFILES_ALL))
CXXFILES    += $(filter-out $(CXXFILES_EXCLUDE), $(CXXFILES_ALL))

COBJS	      := $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o))
CXXOBJS	:= $(patsubst %, $(BUILD)/%, $(CXXFILES:.cpp=.o))

OBJS  := $(COBJS) $(CXXOBJS)
DEPS  := $(OBJS:.o=.o.d)

OBJ_DIRS := $(sort $(dir $(OBJS) $(LIB)))

.PHONY: all
all: $(LIB)
	@$(ECHO) [BUILD] libs 3rd-party $(DIR) done.

$(LIB): $(OBJS) | $(OBJ_DIRS)
	@echo [LD] $@
	@$(AR) rcs $@ $^

$(COBJS) : $(BUILD)/%.o : %.c | $(OBJ_DIRS)
	@echo [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

$(CXXOBJS) : $(BUILD)/%.o : %.cpp | $(OBJ_DIRS)
	@echo [AS] $<
	$(Q)$(CXX) $(CXXFLAGS) -MD -MP -MF $@.d -c $< -o $@

.PHONY: clean
clean:
	@rm -rf $(LIB) $(OBJS) $(DEPS)

$(OBJ_DIRS):
	@$(MKDIR) -p $@

# Header install
HEADER_DIRS := freetype/include:
HEADER_FILES := freetype_wrap.h
HEADER_INSTALL := $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/freetype

include $(SDK_RTSMART_SRC_DIR)/libs/mk/tools_install_header.mk

-include $(DEPS)
