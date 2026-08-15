lib_wlan_offload_dir := $(SDK_RTSMART_BUILD_DIR)/libs/wlan_offload

LIB_CFLAGS += -I$(lib_wlan_offload_dir)/include
LIB_LDFLAGS += -L$(lib_wlan_offload_dir)/lib -lwlan_offload
