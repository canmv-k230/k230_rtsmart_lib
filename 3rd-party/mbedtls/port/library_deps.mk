# The upstream library Makefile does not track the SDK's user configuration.
$(OBJS_CRYPTO) $(OBJS_X509) $(OBJS_TLS): $(wildcard ../../port/*.h)
