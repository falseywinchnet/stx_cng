#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private diagnostic boundary, pinned to zxc 0.15.0. Input and output are
 * disjoint. Encode lengths are complete and <=11. Decode owns no storage;
 * caller supplies n+32 writable bytes in both output and scratch. */
int cng_zxc_huffman_encode(const uint8_t* input, size_t count,
                           const uint32_t* frequencies, const uint8_t* lengths,
                           uint8_t* output, size_t capacity);
int cng_zxc_huffman_decode(const uint8_t* input, size_t size,
                           uint8_t* output, size_t count, uint8_t* scratch);
#ifdef __cplusplus
}
#endif
