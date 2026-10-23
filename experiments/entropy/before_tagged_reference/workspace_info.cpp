#if defined(CNG_EVENT_PARTITIONED)
#include "match_event_partitioned.hpp"
#elif defined(CNG_EVENT_COMPACT)
#include "match_event_compact_backend.hpp"
#elif defined(CNG_EVENT_BACKEND)
#include "match_event_backend.hpp"
#else
#include "match_huffman.hpp"
#endif
#include <iostream>
int main() {
    // Caller workspace only; excludes coder stack tables and input/output.
    std::cout << "{\"encoder_bytes\":" << sizeof(cng_match_trial::EncodeWorkspace)
              << ",\"decoder_bytes\":" << sizeof(cng_match_trial::DecodeWorkspace)
              << ",\"block_bytes\":" << cng_match_trial::block_size << "}\n";
    return 0;
}
