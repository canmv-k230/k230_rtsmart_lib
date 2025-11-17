include ../mkenv.mk

subdirs-$(CONFIG_RTSMART_LIBS_HAL_ENABLE_TESTS) := rtsmart_hal

.PHONY: all clean distclean

all:
	@$(foreach dir,$(subdirs-y),$(MAKE) -C $(dir) all || exit $?;)
	@echo "Make RT-Smart Libraries done."

clean:
	@$(foreach dir,$(subdirs-y),$(MAKE) -C $(dir) clean || exit $?;)
	@echo "Make RT-Smart Libraries clean done."

distclean:
	@$(foreach dir,$(subdirs-y),$(MAKE) -C $(dir) distclean || exit $?;)
	@echo "Make RT-Smart Libraries distclean done."
