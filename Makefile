include mkenv.mk

subdirs-y := 

ifndef RTT_LIBS_DISABLED
subdirs-y += rtsmart_hal 3rd-party
endif

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
