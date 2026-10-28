#ifndef COMPONENTS_HRAY_NATIVE_INCLUDE_HRAY_ENGINE_H_
#define COMPONENTS_HRAY_NATIVE_INCLUDE_HRAY_ENGINE_H_

#include <stdbool.h>
#include <stdint.h>

#include "hray_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All strings and the 32-byte leaf SPKI SHA256 pin are copied before return.
 * server_name supplies the TLS name and default authority; address:port is the
 * dial target. A stream may override its HTTP authority with a Host header.
 * CA validation remains mandatory, including when the pin matches.
 * Returns NULL on invalid parameters or failure to start the network thread.
 * The engine must outlive all streams created from it.
 */
HRAY_NATIVE_EXPORT stream_engine* hray_cronet_engine_create(
    const char* server_name,
    const char* address,
    uint16_t port,
    const uint8_t* pin_sha256,
    bool http3);

HRAY_NATIVE_EXPORT void hray_cronet_engine_destroy(stream_engine* engine);

#ifdef __cplusplus
}
#endif
#endif
