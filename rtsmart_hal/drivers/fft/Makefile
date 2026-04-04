include ../../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

DIR      := $(shell basename `pwd`)
LIB      := $(RTSMART_HAL_LIB_INSTALL_PATH)/lib$(DIR).a
BUILD    := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/$(DIR)

SRCS     := $(wildcard *.c)
OBJS     := $(patsubst %.c, $(BUILD)/%.o, $(SRCS))
DEPS     := $(OBJS:.o=.d)

CINCS    := $(wildcard *.h)
HEADER_TARGETS := $(patsubst %, $(RTSMART_HAL_INC_INSTALL_PATH)/%, $(CINCS))

OBJ_DIRS := $(sort $(dir $(OBJS)) $(RTSMART_HAL_LIB_INSTALL_PATH) $(RTSMART_HAL_INC_INSTALL_PATH))

CFLAGS   := -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os
CFLAGS   += -I. -I$(SDK_SRC_ROOT_DIR)/include -I../../utils
CFLAGS   += -I$(SDK_SRC_ROOT_DIR)/src/rtsmart/mpp/include
CFLAGS   += -I$(SDK_SRC_ROOT_DIR)/src/rtsmart/mpp/include/comm
CFLAGS   += -I$(SDK_SRC_ROOT_DIR)/src/rtsmart/mpp/userapps/api
CFLAGS   += -Wall -Wextra -ffunction-sections -fdata-sections

.PHONY: all clean distclean .FORCE

all: $(LIB) $(HEADER_TARGETS)
	@$(ECHO) [BUILD] libs rtsmart hal drivers $(DIR) done.

$(RTSMART_HAL_INC_INSTALL_PATH)/%.h: %.h | $(RTSMART_HAL_INC_INSTALL_PATH)
	@$(ECHO) "[CP] $< -> $@"
	$(Q)$(CP) $< $@

.FORCE:

$(LIB): $(OBJS) .FORCE | $(RTSMART_HAL_LIB_INSTALL_PATH)
	@$(ECHO) "AR $@"
	$(Q)$(AR) rcs $@ $(OBJS)

$(BUILD)/%.o: %.c | $(OBJ_DIRS)
	@$(ECHO) [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIRS):
	@mkdir -p $@

clean:
	$(Q)$(RM) -rf $(BUILD)

distclean: clean
	$(Q)$(RM) $(LIB)

-include $(DEPS)