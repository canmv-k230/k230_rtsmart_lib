include ../../../mkenv.mk
include ../../../toolchain.mk

DIR = $(shell basename `pwd`)
LIB = $(RTSMART_HAL_LIB_INSTALL_PATH)/lib$(DIR).a
BUILD := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/$(DIR)

CFILES := $(wildcard *.c)
COBJS	:= $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o))
CDEPS	:= $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o.d))

CINCS  := $(wildcard *.h)

CFLAGS := -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany
CFLAGS += -I. -I$(SDK_RTSMART_SRC_DIR)/libs/rtsmart_hal/components/canmv_misc

.PHONY: all clean distclean

HEADER_TARGETS := $(addprefix $(RTSMART_HAL_INC_INSTALL_PATH)/,$(CINCS))

# Rule to copy headers with directory creation dependency
$(HEADER_TARGETS): $(RTSMART_HAL_INC_INSTALL_PATH)/%: %
	@$(MKDIR) -p $(@D)
	@$(CP) $< $@

all: $(LIB) $(HEADER_TARGETS)
	@$(ECHO) "Make lib$(DIR) done."

clean:
	@$(RM) $(COBJS) $(CDEPS)

distclean: clean

$(LIB): $(COBJS)
	@$(MKDIR) -p $(@D)
	@$(ECHO) "AR $@"
	$(Q)$(AR) rcs $@ $^

$(COBJS): $(BUILD)/%.o: %.c
	@$(ECHO) [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

# $(sort $(var)) removes duplicates
#
# The net effect of this, is it causes the objects to depend on the
# object directories (but only for existence), and the object directories
# will be created if they don't exist.
OBJ_DIRS = $(sort $(dir $(COBJS)))
$(COBJS): | $(OBJ_DIRS)
$(OBJ_DIRS):
	@mkdir -p $@

-include $(CDEPS)
