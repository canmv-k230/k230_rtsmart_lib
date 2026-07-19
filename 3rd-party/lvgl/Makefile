include ../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

LIB_CFLAGS :=
LIB_LDFLAGS :=

include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/librtsmart_hal.mk

DIR = $(shell basename `pwd`)
LOCAL_SRC_DIR = $(shell pwd)
LIB = $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)/liblvgl.a
BUILD =  $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/lvgl/

SRC_DIRS := port 

CFILES :=
CFILES_EXCLUDE :=
CFLAGS :=

CXXFILES :=
CXXFILES_EXCLUDE :=
CXXFLAGS :=

CWARN := 
# CWARN += -Wall -Werror
# CWARN += -Wno-int-conversion

C_CXX_FLAGS := -Iport -Ilvgl -I$(SDK_SRC_ROOT_DIR)/include 
C_CXX_FLAGS += -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os $(CWARN) $(LIB_CFLAGS)

ifeq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_FREETYPE),y)
C_CXX_FLAGS += -I$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/freetype
endif

CFLAGS += $(C_CXX_FLAGS) -std=gnu99
CXXFLAGS += $(C_CXX_FLAGS)

CFILES_ALL		= $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.c))
CXXFILES_ALL	= $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.cpp))

include lvgl/lvgl.mk
# Use patsubst to replace the absolute prefix with nothing
CFILES_ALL += $(patsubst $(LOCAL_SRC_DIR)/%,%,$(CSRCS))
CXXFILES_ALL += $(patsubst $(LOCAL_SRC_DIR)/%,%,$(CXXSRCS))

ifneq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL_EXAMPLES),y)
CFILES_EXCLUDE += lvgl/demos/%
CFILES_EXCLUDE += lvgl/examples/%
endif

CFILES      += $(filter-out $(CFILES_EXCLUDE), $(CFILES_ALL))
CXXFILES    += $(filter-out $(CXXFILES_EXCLUDE), $(CXXFILES_ALL))

COBJS	:= $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o))
CXXOBJS	:= $(patsubst %, $(BUILD)/%, $(CXXFILES:.cpp=.o))

OBJS	:= $(COBJS) $(CXXOBJS)
DEPS    := $(OBJS:.o=.o.d)

OBJ_DIRS := $(sort $(dir $(OBJS) $(LIB)))

.PHONY: all
all: $(LIB)
	@$(ECHO) [BUILD] libs 3rd-party $(DIR) done.

$(LIB): $(OBJS) | $(OBJ_DIRS)
	@echo [LD] $@
	$(Q)$(AR) rcs $@ $^

$(COBJS) : $(BUILD)/%.o : %.c | $(OBJ_DIRS)
	@echo [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

$(CXXOBJS) : $(BUILD)/%.o : %.cpp | $(OBJ_DIRS)
	@echo [CXX] $<
	$(Q)$(CXX) $(CXXFLAGS) -MD -MP -MF $@.d -c $< -o $@

.PHONY: clean
clean:
	@rm -rf $(LIB) $(OBJS) $(DEPS)

$(OBJ_DIRS):
	@$(MKDIR) -p $@

-include $(DEPS)
