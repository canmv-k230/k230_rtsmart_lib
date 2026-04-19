include ../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

LIB_CFLAGS :=
LIB_LDFLAGS :=

include $(SDK_RTSMART_SRC_DIR)/libs/mk/librtsmart_hal.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

DIR = $(shell basename `pwd`)
LOCAL_SRC_DIR = $(shell pwd)
LIB = $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)/libmbedtls_port.a
BUILD =  $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/mbedtls/

SRC_DIRS := port

CFILES :=
CFILES_EXCLUDE :=
CFLAGS :=

CXXFILES :=
CXXFILES_EXCLUDE :=
CXXFLAGS :=

CWARN := 
# CWARN += -Wall -Werror
# CWARN += -Wno-int-conversion

CFLAGS += -I. -Iport -Imbedtls/include -DMBEDTLS_USER_CONFIG_FILE=\"mbedtls_port_config.h\" -I$(RTSMART_HAL_INC_INSTALL_PATH) 
CFLAGS += -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os $(CWARN)

CFILES_ALL        = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.c))
CXXFILES_ALL      = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.cpp))

CFILES      += $(filter-out $(CFILES_EXCLUDE), $(CFILES_ALL))
CXXFILES    += $(filter-out $(CXXFILES_EXCLUDE), $(CXXFILES_ALL))

COBJS	:= $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o))
CXXOBJS	:= $(patsubst %, $(BUILD)/%, $(CXXFILES:.cpp=.o))

OBJS	:= $(COBJS) $(CXXOBJS)
DEPS    := $(OBJS:.o=.o.d)

OBJ_DIRS := $(sort $(dir $(OBJS) $(LIB)))

CFLAGS_MBEDTLS := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os -n --static
CFLAGS_MBEDTLS += -DMBEDTLS_USER_CONFIG_FILE=\"mbedtls_port_config.h\"
CFLAGS_MBEDTLS += -I$(LOCAL_SRC_DIR)/port $(LIB_CFLAGS)

LDFLAGS_MBEDTLS := -n --static -T $(LOCAL_SRC_DIR)/port/link.lds -L$(RTSMART_3RD_PARTY_LIB_INSTALL_PATH) -lmbedtls_port $(LIB_CFLAGS) $(LIB_LDFLAGS)

.PHONY: mbedtls_library
mbedtls_library: mbedtls_headers_install
	@$(MAKE) -C mbedtls/library CFLAGS='$(CFLAGS_MBEDTLS)' LDFLAGS='$(LDFLAGS_MBEDTLS)' || exit $?;

.PHONY: mbedtls_library-clean
mbedtls_library-clean:
	@$(MAKE) -C mbedtls/library clean || exit $?;

.PHONY: mbedtls_tests
mbedtls_tests: mbedtls_library $(LIB)
ifeq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_MBEDTLS_TESTS),y)
	@$(MAKE) -C mbedtls/tests mbedtls_test CFLAGS='$(CFLAGS_MBEDTLS)' LDFLAGS='$(LDFLAGS_MBEDTLS)' || exit $?;
	@$(MAKE) -C mbedtls/programs CFLAGS='$(CFLAGS_MBEDTLS)' LDFLAGS='$(LDFLAGS_MBEDTLS)' || exit $?;
	@$(MAKE) -C mbedtls/programs/fuzz CFLAGS='$(CFLAGS_MBEDTLS)' LDFLAGS='$(LDFLAGS_MBEDTLS)' || exit $?;
endif

.PHONY: mbedtls_headers_install
mbedtls_headers_install: check_path
	@cp -rp mbedtls/include/mbedtls $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)
	@cp -rp mbedtls/include/psa $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)
	@cp -rp port/*.h $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)

.PHONY: mbedtls_tests-clean
mbedtls_tests-clean:
ifeq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_MBEDTLS_TESTS),y)
	@$(MAKE) -C mbedtls/tests clean
	@$(MAKE) -C mbedtls/programs clean || exit $?;
endif

.PHONY: all
all: check_path $(LIB) mbedtls_headers_install mbedtls_library mbedtls_tests
	@cp -rp mbedtls/library/libmbedtls.a $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)
	@cp -rp mbedtls/library/libmbedx509.a $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)
	@cp -rp mbedtls/library/libmbedcrypto.a $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)

ifeq ($(CONFIG_RTSMART_3RD_PARTY_ENABLE_MBEDTLS_TESTS),y)
	@mkdir -p $(RTSMART_LIBS_3RD_PARTY_ELF_INSTALL_PATH)/mbedtls/
	@for p in mbedtls/programs/*/* ; do						\
	    if [ -x $$p ] && [ ! -d $$p ] ;     				\
	    then                                				\
	        f=mbedtls_`basename $$p` ;     					\
	        cp $$p $(RTSMART_LIBS_3RD_PARTY_ELF_INSTALL_PATH)/mbedtls/$$f ;	\
	    fi                                  				\
	done
endif

	@$(ECHO) [BUILD] libs 3rd-party $(DIR) done.

$(LIB): $(OBJS) | $(OBJ_DIRS)
	@echo [LD] $@
	$(Q)$(AR) rcs $@ $^

$(COBJS) : $(BUILD)/%.o : %.c | $(OBJ_DIRS)
	@echo [CC] $<
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

$(CXXOBJS) : $(BUILD)/%.o : %.cpp | $(OBJ_DIRS)
	@echo [AS] $<
	$(Q)$(CXX) $(CXXFLAGS) -MD -MP -MF $@.d -c $< -o $@

.PHONY: clean
clean: mbedtls_library-clean mbedtls_tests-clean
	@$(MAKE) -C mbedtls/tests clean
	@rm -rf $(LIB) $(OBJS) $(DEPS)

.PHONY: check_path
check_path:
	@if [ -z "$(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)" ]; then \
		echo "Error: RTSMART_3RD_PARTY_LIB_INSTALL_PATH is not set"; \
		exit 1; \
	fi
	@if [ ! -d "$(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)" ]; then \
		mkdir -p "$(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)"; \
	fi
	@if [ -z "$(RTSMART_3RD_PARTY_INC_INSTALL_PATH)" ]; then \
		echo "Error: RTSMART_3RD_PARTY_INC_INSTALL_PATH is not set"; \
		exit 1; \
	fi
	@if [ ! -d "$(RTSMART_3RD_PARTY_INC_INSTALL_PATH)" ]; then \
		mkdir -p "$(RTSMART_3RD_PARTY_INC_INSTALL_PATH)"; \
	fi

$(OBJ_DIRS):
	@$(MKDIR) -p $@

-include $(DEPS)
