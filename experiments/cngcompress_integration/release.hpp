#pragma once
// Rename the immutable v2 API solely in this comparison tool. The public v3
// library is linked normally; no namespace substitution changes its build.
#define stx_cng cng_release_v2
#define stx_cng_collection cng_release_v2_collection
#include "../release_v2/include/stx_cng/codec.hpp"
#include "../release_v2/include/stx_cng/collection.hpp"
#undef stx_cng_collection
#undef stx_cng
