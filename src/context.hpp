#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#ifndef ZSTD_STATIC_LINKING_ONLY
#define ZSTD_STATIC_LINKING_ONLY
#endif
#include <zstd.h>

namespace stx_cng_detail {
// One-shot, single-threaded level-3 compression without dictionaries. Zstandard
// borrows this aligned workspace and cannot allocate or resize it. Its context
// has no separate destruction operation; the owner releases the whole buffer.
struct CompressionContext {
    static constexpr std::size_t capacity_limit = 4194304;
    std::size_t workspace_bytes = 0;
    std::unique_ptr<std::uint64_t[]> owner;
    ZSTD_CCtx* context = nullptr;

    CompressionContext() {
        static_assert(alignof(std::uint64_t) >= 8);
        if (ZSTD_versionNumber() != 10507) {
            throw std::runtime_error("Zstandard 1.5.7 runtime required");
        }
        std::size_t required = ZSTD_estimateCCtxSize(3);
        if (ZSTD_isError(required) != 0 || required == 0 || required > capacity_limit) {
            throw std::runtime_error("fixed compression workspace bound");
        }
        std::size_t words = (required + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);
        workspace_bytes = words * sizeof(std::uint64_t);
        owner = std::make_unique_for_overwrite<std::uint64_t[]>(words);
        context = ZSTD_initStaticCCtx(owner.get(), workspace_bytes);
        if (context == nullptr) {
            throw std::runtime_error("fixed compression context");
        }
    }
    CompressionContext(const CompressionContext&) = delete;
    CompressionContext& operator=(const CompressionContext&) = delete;
};
}
