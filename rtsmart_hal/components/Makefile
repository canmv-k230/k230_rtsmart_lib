include ../../mkenv.mk

subdirs-y := canmv_misc netmgmt k230_syscalls k230_ota

.PHONY: all clean distclean

all:
	@$(foreach dir,$(subdirs-y),$(MAKE) -C $(dir) all || exit $?;)
	@echo "Make RT-Smart Hal Components done."

clean:
	@$(foreach dir,$(subdirs-y),$(MAKE) -C $(dir) clean || exit $?;)
	@echo "Make RT-Smart Hal Components clean done."

distclean: clean
	@$(foreach dir,$(subdirs-y),$(MAKE) -C $(dir) distclean || exit $?;)
	@echo "Make RT-Smart Hal Components distclean done."
