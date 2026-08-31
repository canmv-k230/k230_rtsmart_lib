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
CONFIG_DEPS := $(SDK_SRC_ROOT_DIR)/.config $(SDK_SRC_ROOT_DIR)/include/generated/autoconf.h

LVGL_UPSTREAM_DIR := $(LOCAL_SRC_DIR)/lvgl
LVGL_UPSTREAM_REV := $(shell git -C $(LVGL_UPSTREAM_DIR) rev-parse HEAD 2>/dev/null)
LVGL_PATCH_DIR := $(LOCAL_SRC_DIR)/port/patches
LVGL_PATCHES := $(sort $(wildcard $(LVGL_PATCH_DIR)/*.patch))
LVGL_PATCH_BUILD_DIR := $(BUILD)/patched
LVGL_PATCH_SRC_DIR := $(LVGL_PATCH_BUILD_DIR)/src
LVGL_PATCH_STAMP := $(LVGL_PATCH_BUILD_DIR)/.source-$(LVGL_UPSTREAM_REV)-patched
LVGL_BUILD_FROM_STAGE ?= 0

ifeq ($(LVGL_BUILD_FROM_STAGE),1)
LVGL_PATH := $(LVGL_PATCH_SRC_DIR)
else
LVGL_PATH := $(LVGL_UPSTREAM_DIR)
endif

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

C_CXX_FLAGS := -Iport -I$(LVGL_PATH) -I$(SDK_SRC_ROOT_DIR)/include
C_CXX_FLAGS += -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os $(CWARN) $(LIB_CFLAGS)

ifeq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_FREETYPE),y)
C_CXX_FLAGS += -I$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/freetype
endif

CFLAGS += $(C_CXX_FLAGS) -std=gnu99
CXXFLAGS += $(C_CXX_FLAGS)

CFILES_ALL		= $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.c))
CXXFILES_ALL	= $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.cpp))

include lvgl/lvgl.mk
LVGL_CFILES_ALL := $(patsubst $(LVGL_PATH)/%,%,$(CSRCS))
LVGL_CXXFILES_ALL := $(patsubst $(LVGL_PATH)/%,%,$(CXXSRCS))
LVGL_CFILES_EXCLUDE :=
LVGL_CXXFILES_EXCLUDE :=

ifneq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL_EXAMPLES),y)
LVGL_CFILES_EXCLUDE += demos/%
LVGL_CFILES_EXCLUDE += examples/%
endif

CFILES      += $(filter-out $(CFILES_EXCLUDE), $(CFILES_ALL))
CXXFILES    += $(filter-out $(CXXFILES_EXCLUDE), $(CXXFILES_ALL))
LVGL_CFILES := $(filter-out $(LVGL_CFILES_EXCLUDE), $(LVGL_CFILES_ALL))
LVGL_CXXFILES := $(filter-out $(LVGL_CXXFILES_EXCLUDE), $(LVGL_CXXFILES_ALL))

COBJS	:= $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o))
CXXOBJS	:= $(patsubst %, $(BUILD)/%, $(CXXFILES:.cpp=.o))
LVGL_COBJS := $(patsubst %.c,$(BUILD)/lvgl/%.o,$(LVGL_CFILES))
LVGL_CXXOBJS := $(patsubst %.cpp,$(BUILD)/lvgl/%.o,$(LVGL_CXXFILES))

OBJS	:= $(COBJS) $(CXXOBJS) $(LVGL_COBJS) $(LVGL_CXXOBJS)
DEPS    := $(OBJS:.o=.o.d)

OBJ_DIRS := $(sort $(dir $(OBJS) $(LIB)))

.PHONY: all
ifeq ($(LVGL_BUILD_FROM_STAGE),1)
all: $(LIB)
	@$(ECHO) [BUILD] libs 3rd-party $(DIR) done.

$(LIB): $(OBJS) $(CONFIG_DEPS) | $(OBJ_DIRS)
	@echo [LD] $@
	$(Q)$(RM) $@
	$(Q)$(AR) rcs $@ $(OBJS)

$(COBJS) : $(BUILD)/%.o : %.c $(CONFIG_DEPS) | $(OBJ_DIRS)
	@echo [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

$(CXXOBJS) : $(BUILD)/%.o : %.cpp $(CONFIG_DEPS) | $(OBJ_DIRS)
	@echo [CXX] $<
	$(Q)$(CXX) $(CXXFLAGS) -MD -MP -MF $@.d -c $< -o $@

$(LVGL_COBJS): $(BUILD)/lvgl/%.o : $(LVGL_PATH)/%.c $(LVGL_PATCH_STAMP) $(CONFIG_DEPS) | $(OBJ_DIRS)
	@echo [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

$(LVGL_CXXOBJS): $(BUILD)/lvgl/%.o : $(LVGL_PATH)/%.cpp $(LVGL_PATCH_STAMP) $(CONFIG_DEPS) | $(OBJ_DIRS)
	@echo [CXX] $<
	$(Q)$(CXX) $(CXXFLAGS) -MD -MP -MF $@.d -c $< -o $@
else
all: $(LVGL_PATCH_STAMP)
	$(Q)$(MAKE) LVGL_BUILD_FROM_STAGE=1 all
endif

$(LVGL_PATCH_STAMP): Makefile $(LVGL_PATCH_DIR) $(LVGL_PATCHES) | $(LVGL_PATCH_BUILD_DIR)
	@echo [SOURCE] preparing patched LVGL source
	@if [ -z "$(LVGL_UPSTREAM_REV)" ]; then echo "[ERROR] $(LVGL_UPSTREAM_DIR) is not a git checkout"; exit 1; fi
	@$(RM) -r $(LVGL_PATCH_SRC_DIR)
	@$(MKDIR) -p $(LVGL_PATCH_SRC_DIR)
	@git -C $(LVGL_UPSTREAM_DIR) archive --format=tar $(LVGL_UPSTREAM_REV) | tar -xf - -C $(LVGL_PATCH_SRC_DIR)
	@set -e; \
	for patch_file in $(LVGL_PATCHES); do \
		echo "[PATCH] $$(basename $$patch_file)"; \
		patch --batch --forward --fuzz=0 -d $(LVGL_PATCH_SRC_DIR) -p1 < $$patch_file; \
	done
	@$(RM) $(LVGL_PATCH_BUILD_DIR)/.source-*-patched
	@touch $@

.PHONY: clean
clean:
	@rm -rf $(LIB) $(BUILD)

$(OBJ_DIRS):
	@$(MKDIR) -p $@

$(LVGL_PATCH_BUILD_DIR):
	@$(MKDIR) -p $@

-include $(DEPS)
