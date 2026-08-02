include ../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

NIMBLE_ROOT := nimble
BUILD := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/nimble
LIB := $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)/libnimble.a

CFILES := $(wildcard $(NIMBLE_ROOT)/nimble/host/src/*.c)
CFILES += \
	$(NIMBLE_ROOT)/nimble/host/services/gap/src/ble_svc_gap.c \
	$(NIMBLE_ROOT)/nimble/host/services/gatt/src/ble_svc_gatt.c \
	$(NIMBLE_ROOT)/nimble/host/store/config/src/ble_store_config.c \
	$(NIMBLE_ROOT)/nimble/host/util/src/addr.c \
	$(NIMBLE_ROOT)/nimble/transport/src/transport.c \
	$(NIMBLE_ROOT)/nimble/transport/src/monitor.c \
	$(NIMBLE_ROOT)/nimble/transport/common/hci_h4/src/hci_h4.c \
	$(NIMBLE_ROOT)/nimble/transport/uart_ll/src/hci_uart.c
CFILES += $(addprefix $(NIMBLE_ROOT)/porting/nimble/src/, \
	endian.c mem.c nimble_port.c os_mbuf.c os_mempool.c os_msys.c)
CFILES += port/src/nimble_npl_os.c port/src/hal_uart.c

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(CFILES))
DEPS := $(OBJS:.o=.d)
OBJ_DIRS := $(sort $(dir $(OBJS) $(LIB)))

NIMBLE_INCLUDES := \
	port/include \
	$(NIMBLE_ROOT) \
	$(NIMBLE_ROOT)/nimble/host/include \
	$(NIMBLE_ROOT)/nimble/host/services/gap/include \
	$(NIMBLE_ROOT)/nimble/host/services/gatt/include \
	$(NIMBLE_ROOT)/nimble/host/store/config/include \
	$(NIMBLE_ROOT)/nimble/host/util/include \
	$(NIMBLE_ROOT)/nimble/include \
	$(NIMBLE_ROOT)/nimble/transport/include \
	$(NIMBLE_ROOT)/nimble/transport/common/hci_h4/include \
	$(NIMBLE_ROOT)/porting/nimble/include \
	$(SDK_RTSMART_SRC_DIR)/libs/3rd-party/mbedtls/mbedtls/include \
	$(SDK_RTSMART_SRC_DIR)/libs/3rd-party/mbedtls/port \
	$(RTSMART_HAL_INC_INSTALL_PATH)

CFLAGS := -std=gnu11 -pthread -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os
CFLAGS += $(addprefix -I,$(NIMBLE_INCLUDES)) -include nimble_port_config.h
CFLAGS += -DMBEDTLS_USER_CONFIG_FILE=\"mbedtls_port_config.h\"
CFLAGS += -Wall -Wextra -ffunction-sections -fdata-sections
CFLAGS += -Wno-maybe-uninitialized -Wno-pointer-arith -Wno-unused-but-set-variable
CFLAGS += -Wno-format -Wno-sign-compare -Wno-old-style-declaration
CFLAGS += -Wno-unused-parameter -Wno-implicit-fallthrough

.PHONY: all clean distclean check-source

all: check-source $(LIB) install-headers
	@$(ECHO) [BUILD] libs 3rd-party nimble done.

check-source:
	@test -f $(NIMBLE_ROOT)/LICENSE || { \
		echo "NimBLE source is missing. Clone https://github.com/apache/mynewt-nimble.git into $(CURDIR)/$(NIMBLE_ROOT)"; \
		exit 1; \
	}

$(LIB): $(OBJS) | $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)
	@$(ECHO) [AR] $@
	$(Q)$(RM) $@
	$(Q)$(AR) rcs $@ $^

$(BUILD)/%.o: %.c | $(OBJ_DIRS)
	@$(ECHO) [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIRS):
	@$(MKDIR) -p $@

clean:
	$(Q)$(RM) -rf $(BUILD) $(LIB)

distclean: clean clean-headers

HEADER_DIRS := \
	port/include: \
	$(NIMBLE_ROOT)/nimble/host/include: \
	$(NIMBLE_ROOT)/nimble/host/services/gap/include: \
	$(NIMBLE_ROOT)/nimble/host/services/gatt/include: \
	$(NIMBLE_ROOT)/nimble/host/store/config/include: \
	$(NIMBLE_ROOT)/nimble/host/util/include: \
	$(NIMBLE_ROOT)/nimble/include: \
	$(NIMBLE_ROOT)/nimble/transport/include: \
	$(NIMBLE_ROOT)/nimble/transport/common/hci_h4/include: \
	$(NIMBLE_ROOT)/porting/nimble/include:
HEADER_INSTALL := $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/nimble

include $(SDK_RTSMART_SRC_DIR)/libs/mk/tools_install_header.mk

-include $(DEPS)
