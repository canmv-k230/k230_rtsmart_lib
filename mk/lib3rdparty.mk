lib3rd_party_inc_dir := \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/cJSON \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/freetype \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/mbedtls \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/minihttp \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/libwebsockets \

ifeq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_NIMBLE),y)
lib3rd_party_inc_dir += $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/nimble
endif

lib3rd_party_lib_dir := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/lib
lib3rd_party_libs := $(filter-out $(lib3rd_party_lib_dir)/libtuya_iot_core.a,$(wildcard $(lib3rd_party_lib_dir)/*))

RTSMART_LINK_NIMBLE ?= $(CONFIG_RTSMART_3RD_PARTY_ENABLE_NIMBLE)
ifneq ($(RTSMART_LINK_NIMBLE),y)
lib3rd_party_libs := $(filter-out $(lib3rd_party_lib_dir)/libnimble.a,$(lib3rd_party_libs))
else
LIB_CFLAGS += -include $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/nimble/nimble_port_config.h
endif

ifeq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_LIBPEER),y)
lib3rd_party_inc_dir += $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/libpeer
libpeer_private_lib_dir := $(SDK_RTSMART_SRC_DIR)/libs/3rd-party/libpeer/3rd-party/lib
libpeer_hal_lib_dir := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/lib
lib3rd_party_lib_dir += $(libpeer_private_lib_dir) $(libpeer_hal_lib_dir)
lib3rd_party_libs += $(libpeer_private_lib_dir)/libsrtp2.a $(libpeer_private_lib_dir)/libusrsctp.a
lib3rd_party_libs += $(wildcard $(libpeer_hal_lib_dir)/*.a)
endif

LIB_CFLAGS += $(addprefix -I, $(lib3rd_party_inc_dir))
LIB_LDFLAGS += $(addprefix -L, $(lib3rd_party_lib_dir)) 
LIB_LDFLAGS += -Wl,--start-group $(addprefix -l,$(subst lib, ,$(basename $(notdir $(lib3rd_party_libs))))) -Wl,--end-group
