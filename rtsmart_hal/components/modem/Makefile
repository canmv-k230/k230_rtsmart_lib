include ../../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

# 1. Path Definitions
DIR      := $(shell basename `pwd`)
LIB      := $(RTSMART_HAL_LIB_INSTALL_PATH)/lib$(DIR).a
BUILD    := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/$(DIR)

# 2. Source and Object Handling
SRCS     := $(wildcard *.c)
OBJS     := $(patsubst %.c, $(BUILD)/%.o, $(SRCS))
DEPS     := $(OBJS:.o=.d)

# 3. Header Installation Mapping
CINCS    := $(wildcard *.h)
HEADER_TARGETS := $(patsubst %, $(RTSMART_HAL_INC_INSTALL_PATH)/%, $(CINCS))

# 4. Directory Creation Logic
OBJ_DIRS := $(sort $(dir $(OBJS)) $(RTSMART_HAL_LIB_INSTALL_PATH) $(RTSMART_HAL_INC_INSTALL_PATH))

# 5. Compiler Flags
# drv_gpio.h / drv_pmu.h / drv_fpioa.h / hal_netmgmt.h are installed into the
# HAL include dir by the drivers and netmgmt component built earlier.
CFLAGS   := -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os
CFLAGS   += -I. -I$(RTSMART_HAL_INC_INSTALL_PATH) -I$(SDK_SRC_ROOT_DIR)/include
CFLAGS   += -Wall -Wextra -ffunction-sections -fdata-sections

.PHONY: all clean distclean

all: $(LIB) $(HEADER_TARGETS)
	@$(ECHO) [BUILD] libs rtsmart hal components $(DIR) done.

# Rule to copy headers only if the local version is newer than the installed version
$(RTSMART_HAL_INC_INSTALL_PATH)/%.h: %.h | $(RTSMART_HAL_INC_INSTALL_PATH)
	@$(ECHO) "[CP] $< -> $@"
	$(Q)$(CP) $< $@

# Rule to archive the library directly into the target folder
$(LIB): $(OBJS) | $(RTSMART_HAL_LIB_INSTALL_PATH)
	@$(ECHO) "AR $@"
	$(Q)$(AR) rcs $@ $^

# Compilation rule for object files
$(BUILD)/%.o: %.c | $(OBJ_DIRS)
	@$(ECHO) [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

# Automatic directory creation
$(OBJ_DIRS):
	@mkdir -p $@

clean:
	$(Q)$(RM) -rf $(BUILD)

distclean: clean
	$(Q)$(RM) $(LIB)

-include $(DEPS)
