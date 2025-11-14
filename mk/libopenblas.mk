
lib_openblas_inc_dir := $(SDK_RTSMART_SRC_DIR)/libs/openblas/include

LIB_CFLAGS += $(addprefix -I, $(lib_openblas_inc_dir))
LIB_LDFLAGS += -L$(SDK_RTSMART_SRC_DIR)/libs/openblas/lib -Wl,--start-group -lopenblas -Wl,--end-group
