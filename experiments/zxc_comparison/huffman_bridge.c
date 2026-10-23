#include "huffman_bridge.h"
#define zxc_huf_encode_section zxc_huf_encode_section_default
#define zxc_huf_decode_section zxc_huf_decode_section_default
#include "zxc_internal.h"
int cng_zxc_huffman_encode(const uint8_t* input, size_t count,
                           const uint32_t* frequencies, const uint8_t* lengths,
                           uint8_t* output, size_t capacity) {
    int result = zxc_huf_encode_section(input, count, frequencies, lengths, output, capacity);
    return result;
}
int cng_zxc_huffman_decode(const uint8_t* input, size_t size,
                           uint8_t* output, size_t count, uint8_t* scratch) {
    int result = zxc_huf_decode_section(input, size, output, count, scratch);
    return result;
}
