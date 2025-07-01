include ../../mkenv.mk
include ../../toolchain.mk

include $(SDK_SRC_ROOT_DIR)/.config
include $(SDK_RTSMART_SRC_DIR)/libs/mk/librtsmart_hal.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/lib3rdparty.mk

DIR = $(shell basename `pwd`)
LOCAL_SRC_DIR = mqttclient
LIB = $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)/lib$(DIR).a
BUILD =  $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/mqttclient

MQTT_PLATFORM = linux

SRC_DIRS :=

CFILES :=
CFILES_EXCLUDE :=
CFLAGS :=

CFILES += $(wildcard $(LOCAL_SRC_DIR)/common/*.c)
CFILES += $(wildcard $(LOCAL_SRC_DIR)/mqtt/*.c)
CFILES += $(wildcard $(LOCAL_SRC_DIR)/mqttclient/*.c)
CFILES += $(wildcard $(LOCAL_SRC_DIR)/network/*.c)
CFILES += $(wildcard $(LOCAL_SRC_DIR)/platform/$(MQTT_PLATFORM)/*.c)

CFLAGS += -I$(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/mqttclient/

ifeq ($(CONFIG_MQTT_NETWORK_ENABLE_TYPE_TLS), y)
	CFILES += $(wildcard $(LOCAL_SRC_DIR)/network/mbedtls/library/*.c)
	CFILES += $(wildcard $(LOCAL_SRC_DIR)/network/mbedtls/wrapper/*.c)

	CFLAGS += -I./include/mbedtls/wrapper
	CFLAGS += -I./include/mbedtls/include
	CFLAGS += -I./include/mbedtls/include/mbedtls
else
	CFLAGS += -DMQTT_NETWORK_TYPE_NO_TLS
endif

ifeq ($(CONFIG_MQTT_LOG_IS_SALOF), y)
	CFILES += $(wildcard $(LOCAL_SRC_DIR)/common/log/*.c)
	CFILES += $(wildcard $(LOCAL_SRC_DIR)/common/log/arch/$(MQTT_PLATFORM)/*.c)

	CFLAGS += -I./include/log
	CFLAGS += -DMQTT_LOG_IS_SALOF
endif

ifeq ($(CONFIG_RTSMART_LIBS_MQTTCLIENT_ENABLE_TESTS), y)
	CFILES += $(wildcard $(LOCAL_SRC_DIR)/example/ali/*.c)
	CFILES += $(wildcard $(LOCAL_SRC_DIR)/example/baidu/*.c)
	CFILES += $(wildcard $(LOCAL_SRC_DIR)/example/onenet/*.c)
#	CFILES += $(wildcard $(LOCAL_SRC_DIR)/example/emqx/*.c)
endif


CXXFILES :=
CXXFILES_EXCLUDE :=
CXXFLAGS :=

CWARN := 

CFLAGS += -std=gnu99 -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany $(CWARN) 

CFILES_ALL        = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.c))
CXXFILES_ALL      = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.cpp))

CFILES      += $(filter-out $(CFILES_EXCLUDE), $(CFILES_ALL))
CXXFILES    += $(filter-out $(CXXFILES_EXCLUDE), $(CXXFILES_ALL))

COBJS	      := $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o))
CXXOBJS		:= $(patsubst %, $(BUILD)/%, $(CXXFILES:.cpp=.o))
OBJS	      := $(COBJS) $(CXXOBJS)

CDEPS	      := $(patsubst %, $(BUILD)/%, $(CFILES:.c=.o.d))
CXXDEPS		:= $(patsubst %, $(BUILD)/%, $(CXXFILES:.cpp=.o.d))
DEPS	      := $(CDEPS) $(CXXDEPS)

.PHONY: all
all: check_path build_inc_dir $(LIB) build_example
	@echo "Make lib$(DIR) done."


.PHONY: build_inc_dir
build_inc_dir:
	@mkdir -p ./include/
	@mkdir -p $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/mqttclient/

	@cp -rp $(wildcard $(LOCAL_SRC_DIR)/common/*.h) ./include
	@cp -rp $(wildcard $(LOCAL_SRC_DIR)/mqtt/*.h) ./include/
	@cp -rp $(wildcard $(LOCAL_SRC_DIR)/mqttclient/*.h) ./include/
	@cp -rp $(wildcard $(LOCAL_SRC_DIR)/network/*.h) ./include/
	@cp -rp $(wildcard $(LOCAL_SRC_DIR)/platform/$(MQTT_PLATFORM)/*.h) ./include/
	@cp -rp ./mqtt_config.h ./include/

	echo $(CONFIG_MQTT_LOG_IS_SALOF)
ifeq ($(CONFIG_MQTT_LOG_IS_SALOF), y)
	@cp -rp $(wildcard $(LOCAL_SRC_DIR)/common/log/*.h) ./include/
endif

ifeq ($(CONFIG_MQTT_NETWORK_ENABLE_TYPE_TLS), y)
	@mkdir -p ./include/mbedtls/wrapper
	@cp -rp $(wildcard $(LOCAL_SRC_DIR)/network/mbedtls/wrapper/*.h) ./include/mbedtls/wrapper/
	
	@cp -rp $(LOCAL_SRC_DIR)/network/mbedtls/include/mbedtls/ ./include/mbedtls/include/
endif

	@cp -rp ./include/* $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/mqttclient/
	@rm -r ./include/


ELF_OUTPUT_DIR := $(RTSMART_LIBS_ELF_INSTALL_PATH)

MQTT_EXAMPLE_CFLAGS := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany
MQTT_EXAMPLE_CFLAGS += -std=gnu99 -fdata-sections -ffunction-sections
MQTT_EXAMPLE_CFLAGS += -T ./link.lds --static -Wl,--gc-sections

MQTT_EXAMPLE_CFLAGS += -I$(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/mqttclient/

MQTT_EXAMPLE_CFLAGS += -I$(SDK_SRC_ROOT_DIR)/include/generated
MQTT_EXAMPLE_CFLAGS += $(LIB_CFLAGS)

example_dirs := 
example_dirs += ali baidu onenet
# example_dirs += emqx

.PHONY: build_example
build_example: check_path build_inc_dir $(LIB)
ifeq ($(CONFIG_RTSMART_LIBS_MQTTCLIENT_ENABLE_TESTS), y)
	@mkdir -p $(RTSMART_LIBS_ELF_INSTALL_PATH)
	@$(foreach obj,$(example_dirs),\
	echo [LD] mqtt_example_$(obj) &&\
	$(CC) $(MQTT_EXAMPLE_CFLAGS) $(patsubst %.c, $(BUILD)/%.o, $(wildcard $(LOCAL_SRC_DIR)/example/$(obj)/$(obj).c)) -o $(ELF_OUTPUT_DIR)/mqtt_example_$(obj) $(LIB) $(LDFLAGS) &&\
	$(STRIP) $(ELF_OUTPUT_DIR)/mqtt_example_$(obj);)
endif


$(LIB): $(OBJS) 
	@echo [LD] $@
	@$(AR) rcs $@ $^

$(COBJS) : $(BUILD)/%.o : %.c
	@echo [CC] $<
	@$(CC) $(CFLAGS) -MD -MP -MF $@.d -c $< -o $@

$(CXXOBJS) : $(BUILD)/%.o : %.cpp
	@echo [AS] $<
	@$(CXX) $(CXXFLAGS) -MD -MP -MF $@.d -c $< -o $@


.PHONY: clean
clean:
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

# $(sort $(var)) removes duplicates
#
# The net effect of this, is it causes the objects to depend on the
# object directories (but only for existence), and the object directories
# will be created if they don't exist.
OBJ_DIRS = $(sort $(dir $(OBJS)))
$(OBJS): | $(OBJ_DIRS)
$(OBJ_DIRS):
	@echo [MKDIR] $@
	@$(MKDIR) -p $@

-include $(DEPS)
