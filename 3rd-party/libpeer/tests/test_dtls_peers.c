#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/random.h>
#include <time.h>

#include "peer.h"
#include "peer_connection.c"

#define PAIRS 4
static int fail_entropy;
static Agent* stalled_agent;
static unsigned int dropped_fragments;

ssize_t __real_getrandom(void* buf, size_t len, unsigned int flags);
ssize_t __wrap_getrandom(void* buf, size_t len, unsigned int flags) {
  if (fail_entropy) {
    errno = EIO;
    return -1;
  }
  return __real_getrandom(buf, len, flags);
}

int __real_agent_recv(Agent* agent, uint8_t* buf, int len);
int __wrap_agent_recv(Agent* agent, uint8_t* buf, int len) {
  int ret = __real_agent_recv(agent, buf, len);
  if (agent == stalled_agent && ret >= 25 && buf[0] == 22 &&
      (buf[19] || buf[20] || buf[21])) {
    dropped_fragments++;
    return 0;
  }
  return ret;
}

static uint64_t now_ms(void) {
  struct timespec ts;
  assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void state_changed(PeerConnectionState state, void* data) {
  (void)state;
  (void)data;
}

static PeerConnection* new_peer(void) {
  PeerConfiguration config = {0};
  config.video_codec = CODEC_H264;
  PeerConnection* pc = peer_connection_create(&config);
  assert(pc != NULL);
  peer_connection_oniceconnectionstatechange(pc, state_changed);
  assert(peer_connection_set_local_ip(pc, "127.0.0.1") == 0);
  return pc;
}

static void configure_fragments(PeerConnection* pc) {
  static const uint16_t groups[] = {MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1, 0};
  mbedtls_ssl_conf_groups(&pc->dtls_srtp.conf, groups);
  mbedtls_ssl_set_mtu(&pc->dtls_srtp.ssl, 256);
  mbedtls_ssl_conf_handshake_timeout(&pc->dtls_srtp.conf, 100, 1000);
}

static void test_entropy_failure(void) {
  PeerConnection* pc = new_peer();
  fail_entropy = 1;
  assert(peer_connection_create_offer(pc) == NULL);
  assert(pc->state == PEER_CONNECTION_FAILED);
  assert(pc->dtls_srtp.cert_cached == 0);
  fail_entropy = 0;
  peer_connection_close(pc);
  assert(peer_connection_create_offer(pc) != NULL);
  assert(pc->dtls_srtp.cert_cached == 1);
  peer_connection_destroy(pc);
}

static void pump(PeerConnection* peers[PAIRS][2], int first_pair) {
  uint64_t deadline = now_ms() + 5000;
  while (now_ms() < deadline) {
    int completed = 0;
    for (int i = 0; i < PAIRS; i++) {
      for (int j = 0; j < 2; j++) {
        uint64_t start = now_ms();
        peer_connection_loop(peers[i][j]);
        assert(now_ms() - start < 500);
        assert(peers[i][j]->state != PEER_CONNECTION_FAILED);
        if (i >= first_pair && peers[i][j]->state == PEER_CONNECTION_COMPLETED) {
          completed++;
        }
      }
    }
    if (completed == (PAIRS - first_pair) * 2) return;
    usleep(1000);
  }
  for (int i = 0; i < PAIRS; i++) {
    fprintf(stderr, "Pair %d states: %s / %s, dropped fragments: %u\n", i,
            peer_connection_state_to_string(peers[i][0]->state),
            peer_connection_state_to_string(peers[i][1]->state), dropped_fragments);
  }
  assert(!"Concurrent DTLS handshakes timed out");
}

int main(void) {
  PeerConnection* peers[PAIRS][2];
  assert(peer_init() == 0);
  test_entropy_failure();
  for (int i = 0; i < PAIRS; i++) {
    peers[i][0] = new_peer();
    peers[i][1] = new_peer();
    const char* offer = peer_connection_create_offer(peers[i][0]);
    assert(offer != NULL);
    peer_connection_set_remote_description(peers[i][1], offer, SDP_TYPE_OFFER);
    const char* answer = peer_connection_create_answer(peers[i][1]);
    assert(answer != NULL);
    peer_connection_set_remote_description(peers[i][0], answer, SDP_TYPE_ANSWER);
    configure_fragments(peers[i][0]);
    configure_fragments(peers[i][1]);
  }

  stalled_agent = &peers[0][0]->agent;
  pump(peers, 1);
  assert(dropped_fragments > 0);
  assert(peers[0][0]->state != PEER_CONNECTION_COMPLETED);
  stalled_agent = NULL;
  pump(peers, 0);
  for (int i = 0; i < PAIRS; i++) {
    for (int j = 0; j < 2; j++) peer_connection_destroy(peers[i][j]);
  }
  peer_deinit();
  puts("Four concurrent fragmented DTLS handshakes and entropy failure tests passed");
  return 0;
}
