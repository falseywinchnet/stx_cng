#ifndef STX_CNGCOMPRESS_H
#define STX_CNGCOMPRESS_H
#include <stddef.h>
#include <stdint.h>
#define STX_CNGCOMPRESS_VERSION_MAJOR 0
#define STX_CNGCOMPRESS_VERSION_MINOR 1
#define STX_CNGCOMPRESS_VERSION_PATCH 0
#define STX_CNGCOMPRESS_FRAME_VERSION 1
#define STX_CNGCOMPRESS_MAX_INPUT_SIZE 2097160u
#ifdef __cplusplus
extern "C" {
#endif

typedef enum stx_cngcompress_status {
    STX_CNGCOMPRESS_OK = 0,
    STX_CNGCOMPRESS_INVALID_ARGUMENT = 1,
    STX_CNGCOMPRESS_INPUT_TOO_LARGE = 2,
    STX_CNGCOMPRESS_OUTPUT_TOO_SMALL = 3,
    STX_CNGCOMPRESS_INVALID_DATA = 4,
    STX_CNGCOMPRESS_OUT_OF_MEMORY = 5,
    STX_CNGCOMPRESS_INTERNAL_ERROR = 6
} stx_cngcompress_status;

typedef struct stx_cngcompress_result {
    stx_cngcompress_status status;
    /* Success: bytes written/queried. OUTPUT_TOO_SMALL: required capacity.
       Other failures: zero. Failed decode output can contain invalid partial data. */
    size_t size;
} stx_cngcompress_result;

typedef struct stx_cngcompress_encoder stx_cngcompress_encoder;
typedef struct stx_cngcompress_decoder stx_cngcompress_decoder;

/* Each handle owns reusable storage. Creation returns NULL on allocation failure.
   Destroy accepts NULL. A handle is used by one call at a time; distinct handles
   can operate concurrently. No input/output pointer is retained after a call. */
stx_cngcompress_encoder* stx_cngcompress_encoder_create(void);
void stx_cngcompress_encoder_destroy(stx_cngcompress_encoder* encoder);
stx_cngcompress_decoder* stx_cngcompress_decoder_create(void);
void stx_cngcompress_decoder_destroy(stx_cngcompress_decoder* decoder);

/* Declared handle storage, excluding allocator overhead and bounded call stack. */
size_t stx_cngcompress_encoder_bytes(void);
size_t stx_cngcompress_decoder_bytes(void);
/* Packed library version: major<<16 | minor<<8 | patch. */
uint32_t stx_cngcompress_version(void);

/* Capacity needed during compression, including temporary block framing.
   Successful emitted frames are at most input_size+5 bytes. */
stx_cngcompress_result stx_cngcompress_bound(size_t input_size);

/* Read a v1 frame header. This does not validate its compressed payload or
   establish content integrity. Frames have no checksum; containers can supply it. */
stx_cngcompress_result stx_cngcompress_decoded_size(const void* input, size_t input_size);

/* Independent bounded frames, not a resumable input/output stream interface.
   Supply exactly one frame to decompress; frame length comes from the container.
   All live input, output and handle storage must be disjoint. NULL buffers are
   accepted only with zero corresponding length/capacity. Input/output lifetimes
   cover the call. Successful calls allocate no storage beyond the handle.
   Preflight argument, header and capacity failures leave output untouched; a later codec
   failure may leave partial output. Handles remain reusable after failure.
   No C++ exception crosses this interface. */
stx_cngcompress_result stx_cngcompress_compress(
    stx_cngcompress_encoder* encoder, const void* input, size_t input_size,
    void* output, size_t output_capacity);
stx_cngcompress_result stx_cngcompress_decompress(
    stx_cngcompress_decoder* decoder, const void* input, size_t input_size,
    void* output, size_t output_capacity);
#ifdef __cplusplus
}
#endif
#endif
