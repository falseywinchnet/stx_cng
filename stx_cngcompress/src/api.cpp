#include "stx_cngcompress.h"
#include "match.hpp"
#include <cstdint>
#include <new>
#include <span>
#include <stdexcept>
struct stx_cngcompress_encoder { stx_cngcompress_match::EncodeWorkspace storage; };
struct stx_cngcompress_decoder { stx_cngcompress_match::DecodeWorkspace storage; };
namespace {
using Status = stx_cngcompress_status;
using Result = stx_cngcompress_result;
Result failure(Status status) { return Result{status, 0}; }
bool overlaps(const void* first, std::size_t first_size, const void* second, std::size_t second_size) {
    if (first_size == 0 || second_size == 0) { return false; }
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(first);
    std::uintptr_t b = reinterpret_cast<std::uintptr_t>(second);
    bool result = a <= b ? b - a < first_size : a - b < second_size;
    return result;
}
bool disjoint(const void* input, std::size_t input_size, void* output, std::size_t output_size,
              const void* workspace, std::size_t workspace_size) {
    if (overlaps(input, input_size, output, output_size)) { return false; }
    if (overlaps(input, input_size, workspace, workspace_size)) { return false; }
    if (overlaps(output, output_size, workspace, workspace_size)) { return false; }
    return true;
}
}
extern "C" {
stx_cngcompress_encoder* stx_cngcompress_encoder_create(void) {
    stx_cngcompress_encoder* result = new (std::nothrow) stx_cngcompress_encoder;
    return result;
}
void stx_cngcompress_encoder_destroy(stx_cngcompress_encoder* encoder) { delete encoder; }
stx_cngcompress_decoder* stx_cngcompress_decoder_create(void) {
    stx_cngcompress_decoder* result = new (std::nothrow) stx_cngcompress_decoder;
    return result;
}
void stx_cngcompress_decoder_destroy(stx_cngcompress_decoder* decoder) { delete decoder; }
size_t stx_cngcompress_encoder_bytes(void) { return sizeof(stx_cngcompress_encoder); }
size_t stx_cngcompress_decoder_bytes(void) { return sizeof(stx_cngcompress_decoder); }
uint32_t stx_cngcompress_version(void) { return 0x000100U; }
stx_cngcompress_result stx_cngcompress_bound(size_t input_size) {
    if (input_size > STX_CNGCOMPRESS_MAX_INPUT_SIZE) { return failure(STX_CNGCOMPRESS_INPUT_TOO_LARGE); }
    std::size_t blocks = (input_size + stx_cngcompress_match::block_size - 1) / stx_cngcompress_match::block_size;
    std::size_t capacity = input_size + 4 * blocks + 5;
    return Result{STX_CNGCOMPRESS_OK, capacity};
}
stx_cngcompress_result stx_cngcompress_decoded_size(const void* input, size_t input_size) {
    if (input == nullptr && input_size != 0) { return failure(STX_CNGCOMPRESS_INVALID_ARGUMENT); }
    if (input_size < 5) { return failure(STX_CNGCOMPRESS_INVALID_DATA); }
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(input);
    if (bytes[0] != 0 && bytes[0] != 16) { return failure(STX_CNGCOMPRESS_INVALID_DATA); }
    std::size_t count = 0;
    for (unsigned byte = 0; byte < 4; ++byte) { count |= static_cast<std::size_t>(bytes[byte + 1]) << (8 * byte); }
    if (count > STX_CNGCOMPRESS_MAX_INPUT_SIZE) { return failure(STX_CNGCOMPRESS_INVALID_DATA); }
    if (bytes[0] == 0 && input_size != count + 5) { return failure(STX_CNGCOMPRESS_INVALID_DATA); }
    if (bytes[0] == 16 && count == 0) { return failure(STX_CNGCOMPRESS_INVALID_DATA); }
    return Result{STX_CNGCOMPRESS_OK, count};
}
stx_cngcompress_result stx_cngcompress_compress(
    stx_cngcompress_encoder* encoder, const void* input, size_t input_size,
    void* output, size_t output_capacity) {
    if (encoder == nullptr || (input == nullptr && input_size != 0) || (output == nullptr && output_capacity != 0)) {
        return failure(STX_CNGCOMPRESS_INVALID_ARGUMENT);
    }
    Result bound = stx_cngcompress_bound(input_size);
    if (bound.status != STX_CNGCOMPRESS_OK) { return bound; }
    if (output_capacity < bound.size) { return Result{STX_CNGCOMPRESS_OUTPUT_TOO_SMALL, bound.size}; }
    if (!disjoint(input, input_size, output, output_capacity, encoder, sizeof(*encoder))) {
        return failure(STX_CNGCOMPRESS_INVALID_ARGUMENT);
    }
    try {
        std::span<const std::uint8_t> source(static_cast<const std::uint8_t*>(input), input_size);
        std::span<std::uint8_t> destination(static_cast<std::uint8_t*>(output), output_capacity);
        std::size_t written = stx_cngcompress_match::encode(source, destination, (*encoder).storage);
        return Result{STX_CNGCOMPRESS_OK, written};
    } catch (const std::bad_alloc&) {
        return failure(STX_CNGCOMPRESS_OUT_OF_MEMORY);
    } catch (...) {
        return failure(STX_CNGCOMPRESS_INTERNAL_ERROR);
    }
}
stx_cngcompress_result stx_cngcompress_decompress(
    stx_cngcompress_decoder* decoder, const void* input, size_t input_size,
    void* output, size_t output_capacity) {
    if (decoder == nullptr || (output == nullptr && output_capacity != 0)) {
        return failure(STX_CNGCOMPRESS_INVALID_ARGUMENT);
    }
    if (!disjoint(input, input_size, output, output_capacity, decoder, sizeof(*decoder))) {
        return failure(STX_CNGCOMPRESS_INVALID_ARGUMENT);
    }
    Result header = stx_cngcompress_decoded_size(input, input_size);
    if (header.status != STX_CNGCOMPRESS_OK) { return header; }
    if (output_capacity < header.size) { return Result{STX_CNGCOMPRESS_OUTPUT_TOO_SMALL, header.size}; }
    try {
        std::span<const std::uint8_t> source(static_cast<const std::uint8_t*>(input), input_size);
        std::span<std::uint8_t> destination(static_cast<std::uint8_t*>(output), header.size);
        stx_cngcompress_match::decode(source, destination, (*decoder).storage);
        return Result{STX_CNGCOMPRESS_OK, header.size};
    } catch (const std::bad_alloc&) {
        return failure(STX_CNGCOMPRESS_OUT_OF_MEMORY);
    } catch (const std::runtime_error&) {
        return failure(STX_CNGCOMPRESS_INVALID_DATA);
    } catch (...) {
        return failure(STX_CNGCOMPRESS_INTERNAL_ERROR);
    }
}
}
