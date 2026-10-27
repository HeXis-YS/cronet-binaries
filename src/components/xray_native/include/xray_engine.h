#ifndef COMPONENTS_XRAY_NATIVE_INCLUDE_XRAY_ENGINE_H_
#define COMPONENTS_XRAY_NATIVE_INCLUDE_XRAY_ENGINE_H_

#include <stdbool.h>
#include <stdint.h>

#include "xray_stream.h"

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
XRAY_NATIVE_EXPORT stream_engine* xray_cronet_engine_create(
    const char* server_name,
    const char* address,
    uint16_t port,
    const uint8_t* pin_sha256,
    bool http3);

XRAY_NATIVE_EXPORT void xray_cronet_engine_destroy(stream_engine* engine);

#ifdef __cplusplus
}
#endif
#endif
