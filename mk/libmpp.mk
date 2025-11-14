# sdk lib and inc
lib_mpp_inc_dir := $(SDK_RTSMART_SRC_DIR)/mpp/include/
lib_mpp_inc_dir += $(SDK_RTSMART_SRC_DIR)/mpp/include/comm
lib_mpp_inc_dir += $(SDK_RTSMART_SRC_DIR)/mpp/include/ioctl
lib_mpp_inc_dir += $(SDK_RTSMART_SRC_DIR)/mpp/userapps/api/

lib_mpp_lib_dir := $(SDK_RTSMART_SRC_DIR)/mpp/userapps/lib/
lib_mpp_lib_dir += $(SDK_RTSMART_SRC_DIR)/mpp/middleware/lib/

LIB_CFLAGS += $(addprefix -I, $(lib_mpp_inc_dir))
LIB_LDFLAGS += $(addprefix -L, $(lib_mpp_lib_dir)) 
LIB_LDFLAGS += -Wl,--start-group $(addprefix -l,$(subst lib, ,$(basename $(notdir $(foreach dir, $(lib_mpp_lib_dir), $(wildcard $(dir)/*)))))) -Wl,--end-group
