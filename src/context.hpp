#pragma once
#include <stx_cngcompress.h>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <stdexcept>

namespace stx_cng_detail {
inline std::size_t entropy_bound(std::size_t size) {
    stx_cngcompress_result result = stx_cngcompress_bound(size);
    if (result.status != STX_CNGCOMPRESS_OK) { throw std::runtime_error("entropy input bound"); }
    return result.size;
}
// The C interface owns each reusable handle. These owners contain that lifetime
// at the CNG operation boundary; source/destination spans are borrowed per call.
struct CompressionContext {
    stx_cngcompress_encoder* context = nullptr;
    CompressionContext() : context(stx_cngcompress_encoder_create()) {
        if (context == nullptr) { throw std::bad_alloc(); }
    }
    ~CompressionContext() { stx_cngcompress_encoder_destroy(context); }
    CompressionContext(const CompressionContext&) = delete;
    CompressionContext& operator=(const CompressionContext&) = delete;
    std::size_t compress(std::span<const std::uint8_t> source, std::span<std::uint8_t> destination) {
        stx_cngcompress_result result = stx_cngcompress_compress(context, source.data(), source.size(), destination.data(), destination.size());
        if (result.status != STX_CNGCOMPRESS_OK) { throw std::runtime_error("entropy compression"); }
        return result.size;
    }
};
struct DecompressionContext {
    stx_cngcompress_decoder* context = nullptr;
    DecompressionContext() : context(stx_cngcompress_decoder_create()) {
        if (context == nullptr) { throw std::bad_alloc(); }
    }
    ~DecompressionContext() { stx_cngcompress_decoder_destroy(context); }
    DecompressionContext(const DecompressionContext&) = delete;
    DecompressionContext& operator=(const DecompressionContext&) = delete;
    std::size_t decompress(std::span<const std::uint8_t> source, std::span<std::uint8_t> destination) {
        stx_cngcompress_result result = stx_cngcompress_decompress(context, source.data(), source.size(), destination.data(), destination.size());
        if (result.status != STX_CNGCOMPRESS_OK) { throw std::runtime_error("entropy decompression"); }
        return result.size;
    }
};
}
