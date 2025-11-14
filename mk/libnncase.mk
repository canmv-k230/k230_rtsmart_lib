
lib_nncase_inc_dir := $(SDK_RTSMART_SRC_DIR)/libs/nncase/riscv64
lib_nncase_inc_dir += $(SDK_RTSMART_SRC_DIR)/libs/nncase/riscv64/nncase/include
lib_nncase_inc_dir += $(SDK_RTSMART_SRC_DIR)/libs/nncase/riscv64/rvvlib/include

LIB_CFLAGS += $(addprefix -I, $(lib_nncase_inc_dir))

LIB_LDFLAGS += -L$(SDK_RTSMART_SRC_DIR)/libs/nncase/riscv64/nncase/lib -L$(SDK_RTSMART_SRC_DIR)/libs/nncase/riscv64/rvvlib 
LIB_LDFLAGS += -Wl,--start-group -lNncase.Runtime.Native -lnncase.rt_modules.k230 -lfunctional_k230 -lrvv -lstdc++ -Wl,--end-group
