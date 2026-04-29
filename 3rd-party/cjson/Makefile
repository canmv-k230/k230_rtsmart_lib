include ../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

DIR = $(shell basename `pwd`)
LIB = $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)/lib$(DIR).a
BUILD = $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/$(DIR)/

CWARN := 
# CWARN += -Wall -Werror

C_CXX_FLAGS := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os $(CWARN) $(LIB_CFLAGS)
C_CXX_FLAGS += -I$(SDK_SRC_ROOT_DIR)/include -include "generated/autoconf.h"

CFLAGS := $(C_CXX_FLAGS) -std=gnu99
CXXFLAGS := $(C_CXX_FLAGS) -std=c++20

CFILES   := \
	cJSON/cJSON.c \
	cJSON/cJSON_Utils.c \

CXXFILES := 

# Fix: Ensure paths are correctly handled for the build directory
COBJS   := $(addprefix $(BUILD)/, $(CFILES:.c=.o))
CXXOBJS := $(addprefix $(BUILD)/, $(CXXFILES:.cpp=.o))

OBJS    := $(COBJS) $(CXXOBJS)
DEPS    := $(OBJS:.o=.o.d)

HEADERS := \
	cJSON/cJSON.h \
	cJSON/cJSON_Utils.h \

HEADER_TARGETS := $(addprefix $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/,$(HEADERS))

OBJ_DIRS := $(sort $(dir $(OBJS) $(LIB) $(HEADER_TARGETS)))

.PHONY: all
all: $(LIB) $(HEADER_TARGETS)
	@$(ECHO) [BUILD] libs 3rd-party $(DIR) done.

$(LIB): $(OBJS) | $(OBJ_DIRS)
	@echo [LD] $@
	$(Q)$(AR) rcs $@ $(OBJS)

# Fix: Static Pattern Rule for C
$(COBJS) : $(BUILD)/%.o : %.c | $(OBJ_DIRS)
	@echo [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

# Fix: Static Pattern Rule for C++
$(CXXOBJS) : $(BUILD)/%.o : %.cpp | $(OBJ_DIRS)
	@echo [CXX] $<
	$(Q)$(CXX) $(CXXFLAGS) -MD -MP -MF $@.d -c $< -o $@

.PHONY: clean
clean:
	@rm -rf $(LIB) $(BUILD) $(HEADER_TARGETS)

$(OBJ_DIRS):
	@$(MKDIR) -p $@

$(HEADER_TARGETS): $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/%: % | $(OBJ_DIRS)
	@$(CP) $< $@

# Include dependencies if they exist
-include $(DEPS)
