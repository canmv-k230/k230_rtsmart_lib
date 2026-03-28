lib_rtsmart_hal_inc_dir := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/include
lib_rtsmart_hal_lib_dir := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/lib

LIB_CFLAGS += $(addprefix -I, $(lib_rtsmart_hal_inc_dir))
LIB_CFLAGS += -I$(SDK_SRC_ROOT_DIR)/include
LIB_LDFLAGS += $(addprefix -L, $(lib_rtsmart_hal_lib_dir)) 
LIB_LDFLAGS += -Wl,--start-group $(addprefix -l,$(subst lib, ,$(basename $(notdir $(foreach dir, $(lib_rtsmart_hal_lib_dir), $(wildcard $(dir)/*)))))) -Wl,--end-group
