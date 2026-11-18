lib_rtsmart_hal_inc_dir := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/include
lib_rtsmart_hal_lib_dir := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/lib

LIB_CFLAGS += $(addprefix -I, $(lib_rtsmart_hal_inc_dir))
LIB_LDFLAGS += $(addprefix -L, $(lib_rtsmart_hal_lib_dir)) 
LIB_LDFLAGS += -Wl,--start-group $(addprefix -l,$(subst lib, ,$(basename $(notdir $(foreach dir, $(lib_rtsmart_hal_lib_dir), $(wildcard $(dir)/*)))))) -Wl,--end-group
# Pull k230_syscalls.o in before libc.a: its overrides of musl's fake-success
# stubs (pread, pipe2, openat, ...) then also serve libc's own callers
# (popen() -> pipe2()), which would otherwise bind to libc's stub.
ifneq ($(wildcard $(addsuffix /libk230_syscalls.a,$(lib_rtsmart_hal_lib_dir))),)
LIB_LDFLAGS += -Wl,-u,__k230_syscalls_anchor
endif
