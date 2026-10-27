#ifndef COMPONENTS_GRPC_SUPPORT_INCLUDE_XRAY_ENGINE_H_
#define COMPONENTS_GRPC_SUPPORT_INCLUDE_XRAY_ENGINE_H_

#include <stdint.h>
#include <stdbool.h>
#include "components/grpc_support/include/bidirectional_stream_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All strings and the 32-byte leaf SPKI SHA256 pin are copied before return.
 * server_name is the TLS and HTTP authority; address:port is the dial target.
 * CA validation remains mandatory, including when the pin matches.
 * Returns NULL on invalid parameters or failure to start the network thread.
 * The engine must outlive all streams created from it.
 */
GRPC_SUPPORT_EXPORT stream_engine* xray_cronet_engine_create(
    const char* server_name,
    const char* address,
    uint16_t port,
    const uint8_t* pin_sha256,
    bool http3);

GRPC_SUPPORT_EXPORT void xray_cronet_engine_destroy(stream_engine* engine);

#ifdef __cplusplus
}
#endif
#endif
