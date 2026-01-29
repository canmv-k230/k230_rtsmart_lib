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
# Maps local .h files to the SDK include installation directory
HEADER_TARGETS := $(patsubst %, $(RTSMART_HAL_INC_INSTALL_PATH)/%, $(CINCS))

# 4. Directory Creation Logic
# Includes build folder, lib path, and include path
OBJ_DIRS := $(sort $(dir $(OBJS)) $(RTSMART_HAL_LIB_INSTALL_PATH) $(RTSMART_HAL_INC_INSTALL_PATH))

# 5. Compiler Flags
CFLAGS   := -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os
CFLAGS   += -I. -I$(SDK_SRC_ROOT_DIR)/include
CFLAGS   += -Wall -Wextra -ffunction-sections -fdata-sections

.PHONY: all clean distclean

# 'all' now ensures the library is built and headers are synchronized
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
