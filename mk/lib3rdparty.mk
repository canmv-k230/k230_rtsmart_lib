lib3rd_party_inc_dir := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include
lib3rd_party_lib_dir := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/lib

lib3rd_party_inc_dir += $(SDK_RTSMART_SRC_DIR)/libs/3rd-party/lvgl/port
lib3rd_party_inc_dir += $(SDK_RTSMART_SRC_DIR)/libs/3rd-party/lvgl/lvgl

LIB_CFLAGS += $(addprefix -I, $(lib3rd_party_inc_dir))
LIB_LDFLAGS += $(addprefix -L, $(lib3rd_party_lib_dir)) 
LIB_LDFLAGS += -Wl,--start-group $(addprefix -l,$(subst lib, ,$(basename $(notdir $(foreach dir, $(lib3rd_party_lib_dir), $(wildcard $(dir)/*)))))) -Wl,--end-group
