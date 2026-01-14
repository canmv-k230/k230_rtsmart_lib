# Include base configuration
include ../../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

# Build configuration
BUILD =  $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/lvgl/tests/

# Source files handling
CFILES := $(wildcard *.c)
OBJS := $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o))
PROGRAMS := $(patsubst %, $(BUILD)/%, $(CFILES:.c=.elf))
PROGRAMDEPS := $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o.d))

OBJ_DIRS := $(sort $(dir $(PROGRAMS)))

# Library configuration
include $(SDK_RTSMART_SRC_DIR)/libs/mk/liblvgl.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/librtsmart_hal.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

# Compiler flags
CFLAGS := -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany
CFLAGS += -I. $(LIB_CFLAGS) -I$(SDK_SRC_ROOT_DIR)/include -include "generated/autoconf.h"
CFLAGS += -Wall -Wextra
CFLAGS += -ffunction-sections -fdata-sections

# Linker flags - removed --static unless specifically needed
LDFLAGS := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static -Wl,--gc-sections
LDFLAGS += $(LIB_LDFLAGS)
# LDFLAGS += -Wl,--print-gc-sections

# Phony targets
.PHONY: all clean distclean check_path

all: $(PROGRAMS)
	@echo "[SUCCESS] Built all LVGL HAL testcases"

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD)

distclean: clean
	@echo "Cleaning installed binaries..."
	@rm -f $(RTSMART_LIBS_ELF_INSTALL_PATH)/*.elf

# Directory creation
$(OBJ_DIRS):
	@mkdir -p $@

# Compilation rule
$(BUILD)/%.o: %.c | $(OBJ_DIRS)
	@echo "[CC] $< -> $@"
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

# Linking rule - separated from compilation
$(BUILD)/%.elf: $(BUILD)/%.o | $(RTSMART_LIBS_ELF_INSTALL_PATH)
	@$(MKDIR) -p $(@D)
	@echo "[LD] $< -> $@"
	$(Q)$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@
	@mv $@ $(RTSMART_LIBS_ELF_INSTALL_PATH)
	@echo "[INSTALLED] $@ to $(RTSMART_LIBS_ELF_INSTALL_PATH)"

$(RTSMART_LIBS_ELF_INSTALL_PATH):
	@$(MKDIR) -p $@

# Include dependencies
-include $(PROGRAMDEPS)
