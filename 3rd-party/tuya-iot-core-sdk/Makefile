include ../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

SDK_DIR := $(shell pwd)
BUILD := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/tuya-iot-core-sdk
LIB := $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)/libtuya_iot_core.a

CFILES := \
	$(SDK_DIR)/src/tuyalink_core.c \
	$(SDK_DIR)/src/cipher_wrapper.c \
	$(SDK_DIR)/src/iotdns.c \
	$(SDK_DIR)/src/tuya_endpoint.c \
	$(SDK_DIR)/middleware/http_client_wrapper.c \
	$(SDK_DIR)/middleware/mqtt_client_wrapper.c \
	$(SDK_DIR)/libraries/coreMQTT/source/core_mqtt.c \
	$(SDK_DIR)/libraries/coreMQTT/source/core_mqtt_state.c \
	$(SDK_DIR)/libraries/coreMQTT/source/core_mqtt_serializer.c \
	$(SDK_DIR)/libraries/coreHTTP/source/core_http_client.c \
	$(SDK_DIR)/libraries/coreHTTP/source/dependency/3rdparty/http_parser/http_parser.c \
	$(SDK_DIR)/libraries/coreJSON/source/core_json.c \
	$(SDK_DIR)/platform/posix/mbedtls_sockets_wrapper.c \
	$(SDK_DIR)/platform/posix/network_wrapper.c \
	$(SDK_DIR)/platform/posix/system_wrapper.c \
	$(SDK_DIR)/platform/posix/storage_wrapper.c \
	$(SDK_DIR)/utils/MultiTimer.c \
	$(SDK_DIR)/utils/base64.c \
	$(SDK_DIR)/utils/cJSON.c \
	$(SDK_DIR)/utils/log.c

CFLAGS :=
CFLAGS += -I$(SDK_DIR)/include
CFLAGS += -I$(SDK_DIR)/interface
CFLAGS += -I$(SDK_DIR)/utils
CFLAGS += -I$(SDK_DIR)/middleware
CFLAGS += -I$(SDK_DIR)/platform/posix
CFLAGS += -I$(SDK_DIR)/libraries/coreMQTT/source/include
CFLAGS += -I$(SDK_DIR)/libraries/coreHTTP/source/include
CFLAGS += -I$(SDK_DIR)/libraries/coreHTTP/source/dependency/3rdparty/http_parser
CFLAGS += -I$(SDK_DIR)/libraries/coreJSON/source/include
CFLAGS += -I$(SDK_RTSMART_SRC_DIR)/libs/3rd-party/mbedtls/mbedtls/include
CFLAGS += -I$(SDK_RTSMART_SRC_DIR)/libs/3rd-party/mbedtls/port
CFLAGS += -DMBEDTLS_USER_CONFIG_FILE=\"mbedtls_port_config.h\"
CFLAGS += -I$(RTSMART_HAL_INC_INSTALL_PATH)
CFLAGS += -I$(RTSMART_3RD_PARTY_INC_INSTALL_PATH)
CFLAGS += -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os

COBJS := $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o))
CDEPS := $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o.d))

.PHONY: all clean build_inc_dir check_path

all: check_path build_inc_dir $(LIB)
	@echo "Make tuya-iot-core-sdk done."

build_inc_dir:
	@mkdir -p $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/tuya
	@cp -rp $(SDK_DIR)/include/*.h $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/tuya
	@cp -rp $(SDK_DIR)/interface/*.h $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/tuya
	@cp -rp $(SDK_DIR)/utils/*.h $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/tuya

$(LIB): $(COBJS)
	@echo [LD] $@
	$(Q)$(AR) rcs $@ $^

$(COBJS): $(BUILD)/%.o : %.c
	@echo [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

clean:
	@rm -rf $(LIB) $(COBJS) $(CDEPS)

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
# The net effect of this, is it causes the objects to depend on the
# object directories (but only for existence), and the object directories
# will be created if they don't exist.
OBJ_DIRS = $(sort $(dir $(COBJS)))
$(COBJS): | $(OBJ_DIRS)
$(OBJ_DIRS):
	@echo [MKDIR] $@
	@$(MKDIR) -p $@

-include $(CDEPS)
