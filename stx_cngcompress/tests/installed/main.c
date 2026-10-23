#include <stx_cngcompress.h>
#include <string.h>
int main(void) {
    unsigned char input[4096];
    unsigned char encoded[4200];
    unsigned char output[4096];
    size_t index = 0;
    int result = 0;
    stx_cngcompress_encoder* encoder = stx_cngcompress_encoder_create();
    stx_cngcompress_decoder* decoder = stx_cngcompress_decoder_create();
    for (index = 0; index < sizeof(input); ++index) { input[index] = (unsigned char)(index % 7); }
    if (encoder == NULL || decoder == NULL) { result = 1; }
    else {
        stx_cngcompress_result bound = stx_cngcompress_bound(sizeof(input));
        stx_cngcompress_result coded = stx_cngcompress_compress(encoder, input, sizeof(input), encoded, sizeof(encoded));
        if (bound.status != STX_CNGCOMPRESS_OK || bound.size > sizeof(encoded) || coded.status != STX_CNGCOMPRESS_OK) { result = 2; }
        else {
            stx_cngcompress_result size = stx_cngcompress_decoded_size(encoded, coded.size);
            stx_cngcompress_result decoded = stx_cngcompress_decompress(decoder, encoded, coded.size, output, sizeof(output));
            if (size.status != STX_CNGCOMPRESS_OK || size.size != sizeof(input) || decoded.status != STX_CNGCOMPRESS_OK
                || decoded.size != sizeof(input) || memcmp(input, output, sizeof(input)) != 0) { result = 3; }
        }
    }
    stx_cngcompress_encoder_destroy(encoder);
    stx_cngcompress_decoder_destroy(decoder);
    return result;
}
