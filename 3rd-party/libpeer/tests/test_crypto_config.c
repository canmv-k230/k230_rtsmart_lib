#include <stdio.h>

#include "../../mbedtls/port/mbedtls_port_config.h"

#if defined(MBEDTLS_ECDH_GEN_PUBLIC_ALT) || defined(MBEDTLS_ECDH_COMPUTE_SHARED_ALT) || defined(MBEDTLS_ECDSA_SIGN_ALT)
#error "Concurrent peers must not use the shared PUF private-key slot"
#endif

#ifndef MBEDTLS_ENTROPY_HARDWARE_ALT
#error "Software key agreement must retain hardware-backed entropy on RT-Smart"
#endif

int main(void) {
  puts("Context-local ECDH/signing configuration test passed");
  return 0;
}
