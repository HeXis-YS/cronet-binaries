// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef COMPONENTS_HRAY_NATIVE_INCLUDE_BIDIRECTIONAL_STREAM_C_H_
#define COMPONENTS_HRAY_NATIVE_INCLUDE_BIDIRECTIONAL_STREAM_C_H_
#include <stdbool.h>
#include <stddef.h>
#if defined(WIN32)
#define HRAY_NATIVE_EXPORT __declspec(dllexport)
#else
#define HRAY_NATIVE_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef struct stream_engine {
  void* obj;
  void* annotation;
} stream_engine;
typedef struct bidirectional_stream {
  void* obj;
  void* annotation;
} bidirectional_stream;
typedef struct bidirectional_stream_header {
  const char* key;
  const char* value;
} bidirectional_stream_header;
typedef struct bidirectional_stream_header_array {
  size_t count;
  const bidirectional_stream_header* headers;
} bidirectional_stream_header_array;

// Callbacks run on the network thread and must not block it. Header storage
// lasts through the callback. Exactly one done callback follows an accepted
// start: error is 0 on success or a Chromium net error (including
// cancellation).
typedef struct bidirectional_stream_callback {
  void (*ready)(bidirectional_stream* stream);
  void (*headers)(bidirectional_stream* stream,
                  const bidirectional_stream_header_array* headers);
  void (*read)(bidirectional_stream* stream, char* data, int bytes_read);
  void (*written)(bidirectional_stream* stream);
  void (*done)(bidirectional_stream* stream, int error);
} bidirectional_stream_callback;

// The engine must outlive its streams. Operations are always posted, including
// from callbacks. Call destroy once after done, or after synchronous start
// rejection. Destroy is asynchronous and must not race other caller operations.
HRAY_NATIVE_EXPORT bidirectional_stream* hray_stream_create(
    stream_engine* engine,
    void* annotation,
    const bidirectional_stream_callback* callbacks);
HRAY_NATIVE_EXPORT void hray_stream_destroy(bidirectional_stream* stream);

// Copies inputs before returning. isolation names the fixed pool group.
// A Host header supplies the optional independent HTTP authority.
// Returns 0 on acceptance, -1 on invalid input. No callbacks follow rejection.
HRAY_NATIVE_EXPORT int hray_stream_start(
    bidirectional_stream* stream,
    const char* url,
    const char* method,
    const bidirectional_stream_header_array* headers,
    const char* isolation,
    bool end_of_stream);

// One read and one write may be in flight independently. The caller retains
// each buffer until its completion callback or done. Zero bytes read is EOF.
// Writes are sent immediately; end_of_stream half-closes the upload.
// Returns 0 for invalid buffer arguments, 1 when posted.
HRAY_NATIVE_EXPORT int hray_stream_read(bidirectional_stream* stream,
                                        char* buffer,
                                        int capacity);
HRAY_NATIVE_EXPORT int hray_stream_write(bidirectional_stream* stream,
                                         const char* buffer,
                                         int count,
                                         bool end_of_stream);
HRAY_NATIVE_EXPORT void hray_stream_cancel(bidirectional_stream* stream);
#ifdef __cplusplus
}
#endif
#endif
