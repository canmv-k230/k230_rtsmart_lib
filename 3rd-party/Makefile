include ../mkenv.mk

include $(SDK_SRC_ROOT_DIR)/.config

subdirs-y := 

subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_FREETYPE) += freetype
subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_MBEDTLS) += mbedtls
subdirs-$(CONFIG_RTSMART_3RD_PARTY_ENABLE_MQTTCLIENT) += mqttclient

.PHONY: all clean distclean

all:
	@$(foreach dir,$(subdirs-y),$(MAKE) -C $(dir) all || exit $?;)
	@echo "Make RT-Smart 3rd-party done."

clean:
	@$(foreach dir,$(subdirs-y),$(MAKE) -C $(dir) clean || exit $?;)
	@echo "Make RT-Smart 3rd-party clean done."

distclean:
	@$(foreach dir,$(subdirs-y),$(MAKE) -C $(dir) distclean || exit $?;)
	@echo "Make RT-Smart 3rd-party distclean done."
