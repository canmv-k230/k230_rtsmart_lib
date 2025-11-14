librtmsart_inc_dir := 
librtmsart_inc_dir += $(SDK_RTSMART_SRC_DIR)/rtsmart/userapps
librtmsart_inc_dir += $(SDK_RTSMART_SRC_DIR)/rtsmart/userapps/sdk/rt-thread/include
librtmsart_inc_dir += $(SDK_RTSMART_SRC_DIR)/rtsmart/userapps/sdk/rt-thread/components/drivers
librtmsart_inc_dir += $(SDK_RTSMART_SRC_DIR)/rtsmart/userapps/sdk/rt-thread/components/drivers

LIB_CFLAGS += $(addprefix -I, $(librtmsart_inc_dir))
LIB_LDFLAGS +=
