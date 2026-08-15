include ../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

BUILD := $(SDK_RTSMART_BUILD_DIR)/libs/wlan_offload
LIB_DIR := $(BUILD)/lib
INC_DIR := $(BUILD)/include
LIB := $(LIB_DIR)/libwlan_offload.a
SRC := src/wlan_offload_client.c
OBJ := $(BUILD)/wlan_offload_client.o
DEP := $(OBJ:.o=.d)
HEADER := $(INC_DIR)/wlan_offload_client.h

CFLAGS := -std=gnu99 -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os
CFLAGS += -Wall -Wextra -Werror -ffunction-sections -fdata-sections
CFLAGS += -Iinclude -Isrc

.PHONY: all clean distclean

all: $(LIB) $(HEADER)
	@$(ECHO) [BUILD] libs wlan_offload done.

$(OBJ): $(SRC) | $(BUILD)
	@$(ECHO) [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $(DEP) -c $< -o $@

$(LIB): $(OBJ) | $(LIB_DIR)
	@$(ECHO) [AR] $@
	$(Q)$(AR) rcs $@ $^

$(HEADER): include/wlan_offload_client.h | $(INC_DIR)
	@$(ECHO) [CP] $<
	$(Q)$(CP) $< $@

$(BUILD) $(LIB_DIR) $(INC_DIR):
	$(Q)$(MKDIR) -p $@

clean:
	$(Q)$(RM) -rf $(BUILD)

distclean: clean

-include $(DEP)
