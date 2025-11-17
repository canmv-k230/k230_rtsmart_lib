include ../../mkenv.mk
include ../../toolchain.mk

include $(SDK_SRC_ROOT_DIR)/.config

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

C_CXX_FLAGS := -Iport -Ilvgl -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os $(CWARN) $(LIB_CFLAGS)

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

CDEPS	:= $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o.d))
CXXDEPS	:= $(patsubst %, $(BUILD)/%, $(CXXFILES:.cpp=.o.d))
DEPS	:= $(CDEPS) $(CXXDEPS)

.PHONY: all
all: check_path $(LIB)
	@echo "Make LVGL wrap done."
ifeq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL_TESTS),y)
	@$(MAKE) -C tests all
endif

$(LIB): $(OBJS)
	@echo [LD] $@
	$(Q)$(AR) rcs $@ $^

$(COBJS) : $(BUILD)/%.o : %.c
	@echo [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

$(CXXOBJS) : $(BUILD)/%.o : %.cpp
	@echo [CXX] $<
	$(Q)$(CXX) $(CXXFLAGS) -MD -MP -MF $@.d -c $< -o $@

.PHONY: clean
clean:
	@rm -rf $(LIB) $(OBJS) $(DEPS)
ifeq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL_TESTS),y)
	@$(MAKE) -C tests clean
endif

.PHONY: check_path
check_path:
	@if [ -z "$(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)" ]; then \
		echo "Error: RTSMART_3RD_PARTY_LIB_INSTALL_PATH is not set"; \
		exit 1; \
	fi
	@if [ ! -d "$(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)" ]; then \
		mkdir -p "$(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)"; \
	fi
	@if [ -z "$(RTSMART_3RD_PARTY_INC_INSTALL_PATH)" ]; then \
		echo "Error: RTSMART_3RD_PARTY_INC_INSTALL_PATH is not set"; \
		exit 1; \
	fi
	@if [ ! -d "$(RTSMART_3RD_PARTY_INC_INSTALL_PATH)" ]; then \
		mkdir -p "$(RTSMART_3RD_PARTY_INC_INSTALL_PATH)"; \
	fi

# $(sort $(var)) removes duplicates
#
# The net effect of this, is it causes the objects to depend on the
# object directories (but only for existence), and the object directories
# will be created if they don't exist.
OBJ_DIRS = $(sort $(dir $(OBJS)))
$(OBJS): | $(OBJ_DIRS)
$(OBJ_DIRS):
	@$(MKDIR) -p $@

-include $(DEPS)
