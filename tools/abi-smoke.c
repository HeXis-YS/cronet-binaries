#define _POSIX_C_SOURCE 200809L
#include "hray_engine.h"

#include <assert.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int done_count;

static void ready(bidirectional_stream* stream) { (void)stream; }
static void headers(bidirectional_stream* stream,
                    const bidirectional_stream_header_array* array) {
  (void)stream;
  (void)array;
}
static void read_complete(bidirectional_stream* stream, char* data, int count) {
  (void)stream;
  (void)data;
  (void)count;
}
static void written(bidirectional_stream* stream) { (void)stream; }
static void done(bidirectional_stream* stream, int error) {
  assert(error == -3);  // ERR_ABORTED, not a connection or certificate failure.
  hray_stream_destroy(stream);
  pthread_mutex_lock(&mutex);
  ++done_count;
  pthread_cond_signal(&condition);
  pthread_mutex_unlock(&mutex);
}

int main(void) {
  const uint8_t pin[32] = {0};
  const bidirectional_stream_callback callbacks = {
      ready, headers, read_complete, written, done};
  const bidirectional_stream_header_array empty_headers = {0, NULL};

  // Keep TCP open without answering TLS so only cancel terminates the request.
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  struct sockaddr_in endpoint = {
      .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  assert(bind(listener, (struct sockaddr*)&endpoint, sizeof(endpoint)) == 0);
  assert(listen(listener, 1) == 0);
  socklen_t endpoint_size = sizeof(endpoint);
  assert(getsockname(listener, (struct sockaddr*)&endpoint, &endpoint_size) == 0);
  uint16_t port = ntohs(endpoint.sin_port);
  char url[64];
  snprintf(url, sizeof(url), "https://localhost:%u/", (unsigned)port);

  assert(hray_cronet_engine_create("", "127.0.0.1", port, pin, false) == NULL);
  stream_engine* default_engine =
      hray_cronet_engine_create("localhost", "127.0.0.1", port, NULL, false);
  assert(default_engine != NULL);
  hray_cronet_engine_destroy(default_engine);
  stream_engine* engine =
      hray_cronet_engine_create("localhost", "127.0.0.1", port, pin, false);
  assert(engine != NULL);
  bidirectional_stream* rejected = hray_stream_create(engine, NULL, &callbacks);
  assert(hray_stream_start(rejected, "http://localhost/", "GET", &empty_headers,
                           "https://abi.hray.invalid", true) == -1);
  hray_stream_destroy(rejected);

  bidirectional_stream* stream = hray_stream_create(engine, &done_count, &callbacks);
  assert(stream->annotation == &done_count);
  assert(hray_stream_read(stream, NULL, 0) == 0);
  assert(hray_stream_write(stream, NULL, -1, false) == 0);
  assert(hray_stream_start(stream, url, "GET", &empty_headers,
                           "https://abi.hray.invalid", true) == 0);
  hray_stream_cancel(stream);
  struct timespec deadline;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += 10;
  pthread_mutex_lock(&mutex);
  while (done_count == 0) {
    assert(pthread_cond_timedwait(&condition, &mutex, &deadline) == 0);
  }
  assert(done_count == 1);
  pthread_mutex_unlock(&mutex);
  hray_cronet_engine_destroy(engine);
  assert(close(listener) == 0);
  puts("ABI 1: C linkage, optional pin, rejected start and ERR_ABORTED cancellation passed");
  return 0;
}
