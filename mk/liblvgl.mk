liblvgl_inc_dir := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include
liblvgl_inc_dir += $(SDK_RTSMART_SRC_DIR)/libs/3rd-party/lvgl/port
liblvgl_inc_dir += $(SDK_RTSMART_SRC_DIR)/libs/3rd-party/lvgl/lvgl

liblvgl_lib_dir := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/lib

LIB_CFLAGS += $(addprefix -I, $(liblvgl_inc_dir))
LIB_LDFLAGS += $(addprefix -L, $(liblvgl_lib_dir)) 
LIB_LDFLAGS += -Wl,--start-group -llvgl -Wl,--end-group
