#include <assert.h>
#include <stdio.h>

/* Inspect stored candidates without adding a test-only public API. */
#include "peer_connection.c"

static char candidates[][160] = {
    "candidate:shared 1 udp 100 192.168.1.2 5000 typ host",
    "candidate:shared 1 udp 100 192.168.1.2 5002 typ host",
    "candidate:shared 1 udp 100 192.168.1.3 5000 typ host",
    "candidate:shared 2 udp 100 192.168.1.2 5000 typ host",
    "candidate:shared 1 udp 100 192.168.1.2 5000 typ srflx raddr 10.0.0.1 rport 6000",
    "candidate:shared 1 udp 100 192.168.1.2 5000 typ relay raddr 10.0.0.1 rport 6000",
    "candidate:shared 1 udp 100 192.168.1.2 5000 typ srflx raddr 10.0.0.2 rport 6000",
    "candidate:shared 1 udp 100 192.168.1.2 5000 typ srflx raddr 10.0.0.1 rport 6002",
};

static char duplicate[] = "candidate:other 1 UDP 200 192.168.1.2 5000 typ host";
static const char credentials[] = "a=ice-ufrag:remote\r\na=ice-pwd:123456789012345678901234\r\n";

static void test_trickle_and_sdp(void) {
  PeerConnection pc = {0};
  Agent agent = {0};
  char sdp[4096];
  size_t offset = (size_t)snprintf(sdp, sizeof(sdp), "%s", credentials);
  int count = sizeof(candidates) / sizeof(candidates[0]);

  for (int i = 0; i < count; i++) {
    assert(peer_connection_add_ice_candidate(&pc, candidates[i]) == 0);
    assert(pc.agent.remote_candidates_count == i + 1);
    int written = snprintf(sdp + offset, sizeof(sdp) - offset, "a=%s\r\n", candidates[i]);
    assert(written > 0 && (size_t)written < sizeof(sdp) - offset);
    offset += (size_t)written;
  }
  assert(peer_connection_add_ice_candidate(&pc, duplicate) == 0);
  assert(pc.agent.remote_candidates_count == count);
  int written = snprintf(sdp + offset, sizeof(sdp) - offset, "a=%s\r\n", duplicate);
  assert(written > 0 && (size_t)written < sizeof(sdp) - offset);
  assert(agent_set_remote_description(&agent, sdp) == 0);
  assert(agent.remote_candidates_count == count);
  for (int i = 0; i < count; i++) {
    assert(ice_candidate_equal(&pc.agent.remote_candidates[i], &agent.remote_candidates[i]));
  }
}

static void test_capacity(void) {
  PeerConnection pc = {0};
  Agent agent = {0};
  char candidate[128];
  char sdp[4096];
  size_t offset = (size_t)snprintf(sdp, sizeof(sdp), "%s", credentials);

  for (int i = 0; i < AGENT_MAX_CANDIDATES; i++) {
    snprintf(candidate, sizeof(candidate),
             "candidate:shared 1 udp 100 192.168.1.2 %d typ host", 5000 + i);
    assert(peer_connection_add_ice_candidate(&pc, candidate) == 0);
    int written = snprintf(sdp + offset, sizeof(sdp) - offset, "a=%s\r\n", candidate);
    assert(written > 0 && (size_t)written < sizeof(sdp) - offset);
    offset += (size_t)written;
  }
  assert(pc.agent.remote_candidates_count == AGENT_MAX_CANDIDATES);
  assert(peer_connection_add_ice_candidate(&pc, duplicate) == 0);
  int written = snprintf(sdp + offset, sizeof(sdp) - offset, "a=%s\r\n", duplicate);
  assert(written > 0 && (size_t)written < sizeof(sdp) - offset);
  assert(agent_set_remote_description(&agent, sdp) == 0);
  assert(agent.remote_candidates_count == AGENT_MAX_CANDIDATES);

  snprintf(candidate, sizeof(candidate),
           "candidate:shared 1 udp 100 192.168.1.2 6000 typ host");
  assert(peer_connection_add_ice_candidate(&pc, candidate) == -1);
  written = snprintf(sdp + offset, sizeof(sdp) - offset, "a=%s\r\n", candidate);
  assert(written > 0 && (size_t)written < sizeof(sdp) - offset);
  assert(agent_set_remote_description(&agent, sdp) == -1);
  assert(agent.remote_candidates_count == AGENT_MAX_CANDIDATES);
  assert(pc.agent.remote_candidates_count == AGENT_MAX_CANDIDATES);
}

static void test_address_and_transport_equality(void) {
  IceCandidate a, b;
  char ipv6[] = "candidate:first 1 udp 100 2001:db8::1 5000 typ host";
  char equivalent[] = "candidate:second 1 UDP 200 2001:db8:0:0:0:0:0:1 5000 typ host";
  assert(ice_candidate_from_description(&a, ipv6, ipv6 + strlen(ipv6)) == 0);
  assert(ice_candidate_from_description(&b, equivalent, equivalent + strlen(equivalent)) == 0);
  assert(ice_candidate_equal(&a, &b));
  strcpy(b.transport, "TCP");
  assert(!ice_candidate_equal(&a, &b));
  assert(!ice_candidate_equal(NULL, &a));
  assert(!ice_candidate_equal(&a, NULL));
}

int main(void) {
  test_trickle_and_sdp();
  test_capacity();
  test_address_and_transport_equality();
  puts("ICE candidate regression tests passed");
  return 0;
}
