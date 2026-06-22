include ../mkenv.mk

subdirs-y := 

subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_FREETYPE) += freetype
subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL) += lvgl
subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_MBEDTLS) += mbedtls
subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_MQTTCLIENT) += mqttclient
subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_MINIHTTP) += minihttp
subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_TUYALINKSDK) += tuya-iot-core-sdk
subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_CJSON) += cjson
subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_LIBPEER) += libpeer

.PHONY: all clean distclean

all: $(subdirs-y)
	@$(ECHO) [BUILD] libs 3rd-party done.

ifneq ($(strip $(subdirs-y)),)
.PHONY: $(subdirs-y)

$(subdirs-y):
	@echo "[BUILD] rtsmart libs 3rd-party $@"
	@$(MAKE) -C $@ all || exit $?;
endif

clean:
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[CLEAN] rtsmart libs 3rd-party $$dir"; \
			$(MAKE) -C $$dir clean || exit $?; \
		done; \
	fi

distclean:
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[DISTCLEAN] rtsmart libs 3rd-party $$dir"; \
			$(MAKE) -C $$dir distclean || exit $?; \
		done; \
	fi
