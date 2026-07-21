lib3rd_party_inc_dir := \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/cJSON \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/freetype \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/mbedtls \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/minihttp \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/libwebsockets \

lib3rd_party_lib_dir := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/lib
lib3rd_party_libs := $(filter-out $(lib3rd_party_lib_dir)/libtuya_iot_core.a,$(wildcard $(lib3rd_party_lib_dir)/*))

LIB_CFLAGS += $(addprefix -I, $(lib3rd_party_inc_dir))
LIB_LDFLAGS += $(addprefix -L, $(lib3rd_party_lib_dir)) 
LIB_LDFLAGS += -Wl,--start-group $(addprefix -l,$(subst lib, ,$(basename $(notdir $(lib3rd_party_libs))))) -Wl,--end-group
