libtuya_inc_dir := \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/tuya \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/mbedtls

libtuya_lib_dir := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/lib

LIB_CFLAGS += $(addprefix -I, $(libtuya_inc_dir))
LIB_LDFLAGS += $(addprefix -L, $(libtuya_lib_dir))
# Resolve static Tuya/MbedTLS dependencies without pulling every archive member.
# Tuya bundles cJSON, so --whole-archive would reintroduce duplicate symbols.
LIB_LDFLAGS += -Wl,--start-group -ltuya_iot_core -lmbedtls -lmbedx509 -lmbedcrypto -lmbedtls_port -Wl,--end-group
