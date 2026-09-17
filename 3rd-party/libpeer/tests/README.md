# Host Regression Tests

Run the focused ICE, RNG, DTLS receive, and crypto-configuration checks with
a host C compiler and AddressSanitizer/UndefinedBehaviorSanitizer:

```sh
make test
```

The integration test also builds the bundled Mbed TLS and SRTP sources for
the host (requires CMake), then negotiates four simultaneous loopback DTLS
connections with fragmented certificates. One peer loses fragments while
the others finish; it must subsequently recover through retransmission.
The test also verifies that entropy failures reject SDP creation and allow
a later retry.

```sh
make integration
```

`BUILD` may point to an external directory. Hardware RNG calls are mocked in
the focused tests; actual hardware entropy and multi-interface streaming
still require testing on a K230 board.
