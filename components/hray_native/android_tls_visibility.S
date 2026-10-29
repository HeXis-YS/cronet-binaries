// LLD applies exports.map before ThinLTO generates these Abseil emulated TLS
// symbols. Declare their visibility in a native object so the late definitions
// stay private to libcronet.so, as required by exports.map.
.hidden __emutls_v._ZN4absl13base_internal19thread_identity_ptrE
.hidden __emutls_t._ZN4absl13base_internal19thread_identity_ptrE

// This object contains no instructions, so it is compatible with BTI, PAC,
// and GCS. Preserve these properties when linking with hardened ARM64 code.
.section .note.gnu.property,"a",%note
.p2align 3
.long 4, 16, 5
.asciz "GNU"
.long 0xc0000000, 4, 7, 0

.section .note.GNU-stack,"",%progbits
