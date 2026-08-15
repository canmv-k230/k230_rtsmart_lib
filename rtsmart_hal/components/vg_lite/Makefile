include ../../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

DIR := $(shell basename `pwd`)
BUILD := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/$(DIR)
LIB := $(RTSMART_HAL_LIB_INSTALL_PATH)/lib$(DIR).a

C_SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS))
DEPS := $(OBJS:.o=.d)
HEADER_TARGETS := $(RTSMART_HAL_INC_INSTALL_PATH)/vg_lite.h
OBJ_DIRS := $(sort $(dir $(OBJS)) $(RTSMART_HAL_LIB_INSTALL_PATH) $(RTSMART_HAL_INC_INSTALL_PATH))

CFLAGS := -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -O2
CFLAGS += -I.
CFLAGS += -Wall -Wextra -ffunction-sections -fdata-sections
CFLAGS += -Wno-multichar -Wno-unused-result -Wno-unused-variable -Wno-format

.PHONY: all clean distclean

all: $(LIB) $(HEADER_TARGETS)
	@$(ECHO) [BUILD] libs rtsmart hal components $(DIR) done.

$(RTSMART_HAL_INC_INSTALL_PATH)/vg_lite.h: vg_lite.h | $(RTSMART_HAL_INC_INSTALL_PATH)
	@$(ECHO) "[CP] $< -> $@"
	$(Q)$(CP) $< $@

$(LIB): $(OBJS) | $(RTSMART_HAL_LIB_INSTALL_PATH)
	@$(ECHO) "[AR] $@"
	$(Q)$(RM) -f $@
	$(Q)$(AR) rcs $@ $^

$(BUILD)/%.o: %.c | $(OBJ_DIRS)
	@$(ECHO) "[CC] $<"
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIRS):
	$(Q)$(MKDIR) -p $@

clean:
	$(Q)$(RM) -rf $(BUILD)

distclean: clean
	$(Q)$(RM) -f $(LIB) $(HEADER_TARGETS)

-include $(DEPS)
