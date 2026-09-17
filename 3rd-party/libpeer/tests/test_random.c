#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

#include "agent.h"
#include "drv_pufs.h"
#include "peer.h"
#include "stun.h"
#include "utils.h"
#include "../../mbedtls/mbedtls/library/entropy_poll.h"

static int open_calls, close_calls, read_calls;
static int read_interrupts, open_failure, read_eof, pufs_read_result;
static size_t bytes_read, fail_after;
static drv_pufs_inst* active_dev;

static void reset_source(void) {
  open_calls = close_calls = read_calls = 0;
  read_interrupts = open_failure = read_eof = pufs_read_result = 0;
  bytes_read = 0;
  fail_after = SIZE_MAX;
  active_dev = NULL;
}

int __wrap_drv_pufs_open(drv_pufs_inst* dev) {
  assert(CONFIG_USE_HWRNG);
  assert(dev != NULL && active_dev == NULL);
  open_calls++;
  if (open_failure) {
    errno = ENOENT;
    return -1;
  }
  memset(dev, 0, sizeof(*dev));
  active_dev = dev;
  return 0;
}

static void fill_bytes(void* output, size_t count) {
  for (size_t i = 0; i < count; i++) {
    ((uint8_t*)output)[i] = (uint8_t)(bytes_read++ % 240 + 1);
  }
}

static ssize_t read_source(void* output, size_t len) {
  read_calls++;
  if (read_interrupts > 0) {
    read_interrupts--;
    errno = EINTR;
    return -1;
  }
  if (bytes_read >= fail_after) {
    errno = EIO;
    return read_eof ? 0 : -1;
  }
  size_t count = len < 3 ? len : 3;
  if (count > fail_after - bytes_read) {
    count = fail_after - bytes_read;
  }
  fill_bytes(output, count);
  return (ssize_t)count;
}

int __wrap_drv_pufs_rng_read(drv_pufs_inst* dev, uint8_t* output, uint32_t len) {
  assert(CONFIG_USE_HWRNG && dev == active_dev && dev->fd == 0);
  assert(output != NULL && len != 0);
  read_calls++;
  if (pufs_read_result != 0) {
    return pufs_read_result;
  }
  if (bytes_read >= fail_after) {
    return -EIO;
  }
  fill_bytes(output, len);
  return 0;
}

int __wrap_drv_pufs_close(drv_pufs_inst* dev) {
  assert(CONFIG_USE_HWRNG && dev == active_dev && dev->fd == 0);
  close_calls++;
  dev->fd = -1;
  active_dev = NULL;
  return 0;
}

ssize_t __wrap_getrandom(void* output, size_t len, unsigned int flags) {
  assert(!CONFIG_USE_HWRNG && flags == 0);
  return read_source(output, len);
}

static void test_reads(void) {
  uint8_t output[12];
  reset_source();
  assert(utils_random_bytes(NULL, 0) == 0);
  assert(utils_random_bytes(NULL, 1) == -1);
  assert(open_calls == 0 && read_calls == 0 && close_calls == 0);

  reset_source();
  read_interrupts = 1;
  assert(utils_random_bytes(output, sizeof(output)) == 0);
  assert(read_calls == (CONFIG_USE_HWRNG ? 1 : 5));
  assert(open_calls == CONFIG_USE_HWRNG && close_calls == CONFIG_USE_HWRNG);
  for (size_t i = 0; i < sizeof(output); i++) {
    assert(output[i] == i + 1);
  }

#if CONFIG_USE_HWRNG
  reset_source();
  open_failure = 1;
  assert(utils_random_bytes(output, sizeof(output)) == -1);
  assert(open_calls == 1 && close_calls == 0 && read_calls == 0);
  open_failure = 0;
  assert(utils_random_bytes(output, sizeof(output)) == 0);
  assert(open_calls == 2 && close_calls == 1);

  int errors[] = {-EIO, 1};
  for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
    reset_source();
    pufs_read_result = errors[i];
    assert(utils_random_bytes(output, sizeof(output)) == -1);
    assert(open_calls == 1 && read_calls == 1 && close_calls == 1);
    assert(active_dev == NULL);
    pufs_read_result = 0;
    assert(utils_random_bytes(output, sizeof(output)) == 0);
    assert(open_calls == 2 && close_calls == 2);
  }
#if SIZE_MAX > UINT32_MAX
  reset_source();
  assert(utils_random_bytes(output, (size_t)UINT32_MAX + 1) == -1);
  assert(open_calls == 0 && read_calls == 0 && close_calls == 0);
#endif
#else
  for (int eof = 0; eof <= 1; eof++) {
    reset_source();
    fail_after = 3;
    read_eof = eof;
    assert(utils_random_bytes(output, sizeof(output)) == -1);
    assert(bytes_read == 3);
    assert(open_calls == 0 && close_calls == 0);
  }
#endif
}

static void test_credentials(void) {
  Agent agent = {0};
  StunMessage message = {0};
  char text[25];
  reset_source();
  assert(utils_random_string(text, 24) == 0);
  assert(strlen(text) == 24);
  assert(strspn(text, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") == 24);
  assert(agent_create_ice_credential(&agent) == 0);
  assert(strlen(agent.local_ufrag) == 4 && strlen(agent.local_upwd) == 24);
  assert(stun_msg_create(&message, STUN_METHOD_BINDING) == 0);
  assert(message.size == sizeof(StunHeader));

  reset_source();
  fail_after = 32;
  assert(agent_create_ice_credential(&agent) == -1);
  assert(agent.local_ufrag[0] == '\0' && agent.local_upwd[0] == '\0');
  assert(stun_msg_create(&message, STUN_METHOD_BINDING) == -1);
  assert(message.size == 0);
  assert(utils_random_string(text, 24) == -1 && text[0] == '\0');
  assert(open_calls == close_calls);
}

static void test_public_random_api(void) {
  uint8_t output[16];
  reset_source();
  assert(peer_random_bytes(NULL, 0) == 0);
  assert(peer_random_bytes(NULL, 1) == -1);
  assert(peer_random_bytes(output, sizeof(output)) == 0);
  assert(bytes_read == sizeof(output));
  fail_after = bytes_read;
  assert(peer_random_bytes(output, sizeof(output)) == -1);
  assert(open_calls == close_calls);
}

#if CONFIG_USE_HWRNG
static void* random_worker(void* arg) {
  uint8_t output[32];
  (void)arg;

  for (int i = 0; i < 100; i++) {
    assert(utils_random_bytes(output, sizeof(output)) == 0);
  }
  return NULL;
}

static void test_concurrent_reads(void) {
  pthread_t threads[8];
  reset_source();

  for (size_t i = 0; i < sizeof(threads) / sizeof(threads[0]); i++) {
    assert(pthread_create(&threads[i], NULL, random_worker, NULL) == 0);
  }
  for (size_t i = 0; i < sizeof(threads) / sizeof(threads[0]); i++) {
    assert(pthread_join(threads[i], NULL) == 0);
  }

  assert(open_calls == 800 && read_calls == 800 && close_calls == 800);
  assert(active_dev == NULL);
}

static void test_mbedtls_entropy(void) {
  uint8_t output[16];
  size_t len = 99;
  reset_source();
  assert(mbedtls_hardware_poll(NULL, NULL, 0, &len) == 0 && len == 0);
  assert(open_calls == 0);
  assert(mbedtls_hardware_poll(NULL, output, sizeof(output), &len) == 0);
  assert(len == sizeof(output) && bytes_read == len);
  assert(open_calls == 1 && close_calls == 1);

  open_failure = 1;
  assert(mbedtls_hardware_poll(NULL, output, sizeof(output), &len) != 0);
  assert(len == 0 && close_calls == 1);
  open_failure = 0;
  pufs_read_result = -EIO;
  assert(mbedtls_hardware_poll(NULL, output, sizeof(output), &len) != 0);
  assert(len == 0 && close_calls == 2);
}
#endif

int main(void) {
  test_reads();
  test_credentials();
  test_public_random_api();
#if CONFIG_USE_HWRNG
  test_concurrent_reads();
  test_mbedtls_entropy();
#endif
  printf("Random-source regression tests passed (hardware=%d)\n", CONFIG_USE_HWRNG);
  return 0;
}
