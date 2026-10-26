# Zstandard 1.5.7, the codec's tables frame library, built privately from its official release tarball, which is
# checked in at third_party/zstd-1.5.7.tar.gz and pinned by SHA-256. Configuration verifies the archive, extracts only
# lib/ and the licence into the build tree, and checks the licence against the repository's copy. Only the library
# sources are compiled (no programs, dictionary builder, deprecated or legacy formats), single threaded and without
# assembly; their objects become part of libstx_cng, so nothing else is installed or linked. No system library and
# no download is involved.
set(STX_CNG_ZSTD_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd-1.5.7.tar.gz")
set(STX_CNG_ZSTD_SHA256 eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3)
if(NOT EXISTS "${STX_CNG_ZSTD_ARCHIVE}")
    message(FATAL_ERROR "third_party/zstd-1.5.7.tar.gz is missing")
endif()
file(SHA256 "${STX_CNG_ZSTD_ARCHIVE}" stx_cng_zstd_actual)
if(NOT stx_cng_zstd_actual STREQUAL STX_CNG_ZSTD_SHA256)
    message(FATAL_ERROR "Zstandard archive hash ${stx_cng_zstd_actual} is not the pinned ${STX_CNG_ZSTD_SHA256}")
endif()
set(STX_CNG_ZSTD_ROOT "${CMAKE_CURRENT_BINARY_DIR}/zstd/zstd-1.5.7")
if(NOT EXISTS "${STX_CNG_ZSTD_ROOT}/lib/zstd.h")
    file(ARCHIVE_EXTRACT INPUT "${STX_CNG_ZSTD_ARCHIVE}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/zstd"
        PATTERNS zstd-1.5.7/lib zstd-1.5.7/LICENSE)
endif()
file(SHA256 "${STX_CNG_ZSTD_ROOT}/LICENSE" stx_cng_zstd_licence)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-zstd.txt" stx_cng_zstd_licence_copy)
if(NOT stx_cng_zstd_licence STREQUAL stx_cng_zstd_licence_copy)
    message(FATAL_ERROR "LICENSE-zstd.txt differs from Zstandard 1.5.7's LICENSE")
endif()
set(STX_CNG_ZSTD_INCLUDE "${STX_CNG_ZSTD_ROOT}/lib")
set(stx_cng_zstd_lib "${STX_CNG_ZSTD_ROOT}/lib")
add_library(stx_cng_zstd OBJECT
    ${stx_cng_zstd_lib}/common/debug.c
    ${stx_cng_zstd_lib}/common/entropy_common.c
    ${stx_cng_zstd_lib}/common/error_private.c
    ${stx_cng_zstd_lib}/common/fse_decompress.c
    ${stx_cng_zstd_lib}/common/pool.c
    ${stx_cng_zstd_lib}/common/threading.c
    ${stx_cng_zstd_lib}/common/xxhash.c
    ${stx_cng_zstd_lib}/common/zstd_common.c
    ${stx_cng_zstd_lib}/compress/fse_compress.c
    ${stx_cng_zstd_lib}/compress/hist.c
    ${stx_cng_zstd_lib}/compress/huf_compress.c
    ${stx_cng_zstd_lib}/compress/zstd_compress.c
    ${stx_cng_zstd_lib}/compress/zstd_compress_literals.c
    ${stx_cng_zstd_lib}/compress/zstd_compress_sequences.c
    ${stx_cng_zstd_lib}/compress/zstd_compress_superblock.c
    ${stx_cng_zstd_lib}/compress/zstd_double_fast.c
    ${stx_cng_zstd_lib}/compress/zstd_fast.c
    ${stx_cng_zstd_lib}/compress/zstd_lazy.c
    ${stx_cng_zstd_lib}/compress/zstd_ldm.c
    ${stx_cng_zstd_lib}/compress/zstd_opt.c
    ${stx_cng_zstd_lib}/compress/zstd_preSplit.c
    ${stx_cng_zstd_lib}/compress/zstdmt_compress.c
    ${stx_cng_zstd_lib}/decompress/huf_decompress.c
    ${stx_cng_zstd_lib}/decompress/zstd_ddict.c
    ${stx_cng_zstd_lib}/decompress/zstd_decompress.c
    ${stx_cng_zstd_lib}/decompress/zstd_decompress_block.c)
target_compile_definitions(stx_cng_zstd PRIVATE ZSTD_LEGACY_SUPPORT=0 ZSTD_DISABLE_ASM)
target_include_directories(stx_cng_zstd SYSTEM PUBLIC "${stx_cng_zstd_lib}")
set_target_properties(stx_cng_zstd PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
