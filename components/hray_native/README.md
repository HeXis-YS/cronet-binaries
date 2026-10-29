# hray native transport ABI 1

Build `//components/hray_native:cronet` to produce `libcronet.so`.
The public C headers are `include/hray_engine.h` and `include/hray_stream.h`.
Linux and Android export exactly their eight functions under `HRAY_CRONET_1.0`;
Chromium and C++ symbols remain private. These function signatures, structure layouts,
callback field order, buffer ownership and return values define ABI 1. Compatible
Chromium updates do not require rebuilding hray-core.

An engine connects directly to `address:port`, using `server_name` for TLS
verification and the default HTTP authority. `pin_sha256` selects certificate
verification:

| Argument | Verification |
| --- | --- |
| `NULL` | Chromium's default CA, hostname and certificate validity checks. |
| Pointer to 32 bytes | Leaf SPKI SHA-256 and hostname must match; CA trust and certificate validity dates are not checked. This supports self-signed certificates. |

TLS handshake signatures are verified in both modes to prove possession of
the private key. An all-zero hash is a supplied pin, not an absent pin. The C
signatures and structure layouts are unchanged; this replaces the previous
mandatory CA-plus-pin policy.

A request's Host header overrides HTTP authority without changing TLS identity.
`http3` selects forced HTTP/3; otherwise requests use HTTP/2. The isolation argument
retains the existing fixed grouping behavior; connection rotation is outside
this ABI.

The request URL must use the engine's TLS name and port. Chromium derives the
TLS identity and connection port from that URL; the engine maps the name to
the dial address and configures forced HTTP/3 for that origin. hray-core sets
this URL authority before sending, independently of the Host header.

Create engines on a caller thread, outside network callbacks. Engine creation
copies its strings and any supplied pin before returning. Keep the engine alive
until all its streams have been destroyed. Keep the library loaded for the process
lifetime because it owns Chromium's shared runtime and network thread.

Create a stream with five non-null callbacks and an optional caller-owned
annotation. The library owns `obj`; callers may read `annotation` in callbacks.
Call start once. It copies the URL, method, headers and isolation string.
Start returns 0 when accepted and -1 for invalid input; rejection produces no
callbacks. Destroy rejected streams directly.

Callbacks run on the network thread and must not block it. Ready permits
upload writes; headers permits response reads. Header memory is valid only
inside the headers callback. One read and one write may be outstanding at the
same time. Read/write return 1 when queued and 0 for invalid buffer arguments.
The caller owns each buffer until its completion callback or done. A read of
zero bytes marks download EOF; an end-of-stream write closes the upload.
An empty upload can instead use end-of-stream on start.

Every accepted start ends with exactly one done callback: 0 for success,
otherwise a Chromium net error (cancellation is `ERR_ABORTED`, -3). Transport
I/O stops before done, so outstanding buffers can then be released. No later
callbacks follow. Call destroy once from done; destruction is queued rather
than performed on the callback stack. Operations, including those called from
callbacks, are queued on the network thread. Caller operations must be
serialized and must not race destroy. Handles and all supplied pointers must
be valid for the documented lifetimes.

Run the Linux build and ABI smoke check through the automation branch's
`tools/build-linux.sh`. See that branch's README for setup and invocation.
