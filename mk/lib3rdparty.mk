lib3rd_party_inc_dir := \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/cJSON \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/freetype \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/mbedtls \
	$(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include/minihttp \

lib3rd_party_lib_dir := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/lib

LIB_CFLAGS += $(addprefix -I, $(lib3rd_party_inc_dir))
LIB_LDFLAGS += $(addprefix -L, $(lib3rd_party_lib_dir)) 
LIB_LDFLAGS += -Wl,--start-group $(addprefix -l,$(subst lib, ,$(basename $(notdir $(foreach dir, $(lib3rd_party_lib_dir), $(wildcard $(dir)/*)))))) -Wl,--end-group
