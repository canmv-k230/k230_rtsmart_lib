#include <assert.h>
#include <stdio.h>

#include "peer_connection.c"

static int recv_calls;
static int recv_result;
static const uint8_t* recv_data;
static const uint8_t fragment[] = {
    22, 0xfe, 0xfd, 0, 0, 0, 0, 0, 0, 0, 1, 0, 14,
    11, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 2, 0xaa, 0xbb,
};
static const uint8_t client_hello_fragment_1[] = {
    22, 0xfe, 0xfd, 0, 0, 0, 0, 0, 0, 0, 1, 0, 16,
    1, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 4, 0xaa, 0xbb, 0xcc, 0xdd,
};
static const uint8_t client_hello_fragment_2[] = {
    22, 0xfe, 0xfd, 0, 0, 0, 0, 0, 0, 0, 2, 0, 14,
    1, 0, 0, 6, 0, 0, 0, 0, 4, 0, 0, 2, 0xee, 0xff,
};
static const uint8_t client_hello_complete[] = {
    22, 0xfe, 0xfd, 0, 0, 0, 0, 0, 0, 0, 1, 0, 18,
    1, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 6,
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
};

int agent_recv(Agent* agent, uint8_t* buf, int len) {
  (void)agent;
  recv_calls++;
  if (recv_result > 0) {
    assert(len >= recv_result);
    memcpy(buf, recv_data, recv_result);
  }
  return recv_result;
}

void ports_sleep_ms(int ms) {
  (void)ms;
  assert(!"DTLS receive must never wait for another fragment");
}

int main(void) {
  PeerConnection peers[2] = {0};
  uint8_t buf[128];
  for (size_t i = 0; i < 2; i++) {
    peers[i].dtls_srtp.user_data = &peers[i];
    peers[i].dtls_recv_available = 1;
  }

  recv_data = fragment;
  recv_result = sizeof(fragment);
  assert(peer_connection_dtls_srtp_recv(&peers[0].dtls_srtp, buf, sizeof(buf)) == sizeof(fragment));
  assert(memcmp(buf, fragment, sizeof(fragment)) == 0);
  assert(recv_calls == 1);
  assert(peer_connection_dtls_srtp_recv(&peers[0].dtls_srtp, buf, sizeof(buf)) == MBEDTLS_ERR_SSL_WANT_READ);
  assert(recv_calls == 1);
  assert(peer_connection_dtls_srtp_recv(&peers[1].dtls_srtp, buf, sizeof(buf)) == sizeof(fragment));
  assert(recv_calls == 2);

  peers[0].dtls_recv_available = 1;
  recv_result = 0;
  assert(peer_connection_dtls_srtp_recv(&peers[0].dtls_srtp, buf, sizeof(buf)) == MBEDTLS_ERR_SSL_WANT_READ);
  assert(recv_calls == 3);

  peers[0].dtls_recv_available = 1;
  peers[0].agent_ret = sizeof(fragment);
  memcpy(peers[0].agent_buf, fragment, sizeof(fragment));
  assert(peer_connection_dtls_srtp_recv(&peers[0].dtls_srtp, buf, sizeof(buf)) == sizeof(fragment));
  assert(memcmp(buf, fragment, sizeof(fragment)) == 0);
  assert(peers[0].agent_ret == 0 && recv_calls == 3);

  peers[0].dtls_recv_available = 1;
  recv_data = client_hello_fragment_1;
  recv_result = sizeof(client_hello_fragment_1);
  assert(peer_connection_dtls_srtp_recv(&peers[0].dtls_srtp, buf, sizeof(buf)) ==
         MBEDTLS_ERR_SSL_WANT_READ);
  assert(peers[0].dtls_client_hello_received == 4);

  peers[0].dtls_recv_available = 1;
  recv_data = client_hello_fragment_2;
  recv_result = sizeof(client_hello_fragment_2);
  assert(peer_connection_dtls_srtp_recv(&peers[0].dtls_srtp, buf, sizeof(buf)) ==
         sizeof(client_hello_complete));
  assert(memcmp(buf, client_hello_complete, sizeof(client_hello_complete)) == 0);
  assert(peers[0].dtls_client_hello == NULL);
  assert(peers[0].dtls_client_hello_map == NULL);

  peers[0].dtls_recv_available = 1;
  peers[0].agent_ret = sizeof(fragment);
  assert(peer_connection_dtls_srtp_recv(&peers[0].dtls_srtp, buf, 1) == MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL);
  assert(peers[0].agent_ret == 0 && recv_calls == 5);

  peers[0].dtls_recv_available = 1;
  recv_result = -1;
  assert(peer_connection_dtls_srtp_recv(&peers[0].dtls_srtp, buf, sizeof(buf)) == -1);
  puts("Nonblocking DTLS receive regression tests passed");
  return 0;
}
