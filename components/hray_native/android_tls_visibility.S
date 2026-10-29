// LLD applies exports.map before ThinLTO generates these Abseil emulated TLS
// symbols. Declare their visibility in a native object so the late definitions
// stay private to libcronet.so, as required by exports.map.
.hidden __emutls_v._ZN4absl13base_internal19thread_identity_ptrE
.hidden __emutls_t._ZN4absl13base_internal19thread_identity_ptrE

.section .note.GNU-stack,"",%progbits
