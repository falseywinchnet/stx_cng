#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>
namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::uint32_t big(std::span<const std::uint8_t> input, std::size_t position) {
    require(position <= input.size() && input.size() - position >= 4, "diagnostic integer extent");
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) { value = (value << 8) | input[position + byte]; }
    return value;
}
struct Context {
    ZSTD_CCtx* encoder = ZSTD_createCCtx();
    Context() { require(encoder != nullptr, "diagnostic context"); }
    ~Context() { ZSTD_freeCCtx(encoder); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};
void inspect(std::span<const std::uint8_t> frame, const char* kind) {
    unsigned long long declared = ZSTD_getFrameContentSize(frame.data(), frame.size());
    require(declared <= 2097160, "diagnostic frame bound");
    Bytes raw(static_cast<std::size_t>(declared));
    std::size_t unpacked = ZSTD_decompress(raw.data(), raw.size(), frame.data(), frame.size());
    require(ZSTD_isError(unpacked) == 0 && unpacked == raw.size(), "diagnostic frame inverse");
    Context context;
    Bytes control(ZSTD_compressBound(raw.size()));
    std::size_t control_size = ZSTD_compressCCtx(context.encoder, control.data(), control.size(), raw.data(), raw.size(), 3);
    require(ZSTD_isError(control_size) == 0 && control_size == frame.size()
            && std::equal(frame.begin(), frame.end(), control.begin()), "diagnostic frozen control bytes");
    require(ZSTD_isError(ZSTD_CCtx_reset(context.encoder, ZSTD_reset_session_and_parameters)) == 0, "diagnostic reset");
    require(ZSTD_isError(ZSTD_CCtx_setParameter(context.encoder, ZSTD_c_compressionLevel, 3)) == 0, "diagnostic level");
    std::vector<ZSTD_Sequence> sequences(ZSTD_sequenceBound(raw.size()));
    // This deprecated API is explicitly informational. Every generated reference
    // is checked against source bytes; no generated parse enters the candidate.
    std::size_t count = ZSTD_generateSequences(context.encoder, sequences.data(), sequences.size(), raw.data(), raw.size());
    require(ZSTD_isError(count) == 0 && count <= sequences.size(), "diagnostic generation");
    std::size_t cursor = 0;
    std::size_t literals = 0;
    std::size_t copied = 0;
    std::size_t short_bytes = 0;
    std::size_t distant_bytes = 0;
    std::size_t cross_block_bytes = 0;
    std::size_t repeat_bytes = 0;
    std::size_t maximum_distance = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const ZSTD_Sequence& sequence = sequences[index];
        require(sequence.litLength <= raw.size() - cursor, "diagnostic literals");
        cursor += sequence.litLength;
        literals += sequence.litLength;
        if (sequence.matchLength == 0) { continue; }
        require(sequence.offset != 0 && sequence.offset <= cursor && sequence.matchLength <= raw.size() - cursor, "diagnostic reference bound");
        for (std::size_t byte = 0; byte < sequence.matchLength; ++byte) {
            require(raw[cursor + byte] == raw[cursor + byte - sequence.offset], "diagnostic exact reference");
        }
        if (sequence.matchLength < 8) { short_bytes += sequence.matchLength; }
        if (sequence.offset > 65535) { distant_bytes += sequence.matchLength; }
        if ((cursor - sequence.offset) / 131072 != cursor / 131072) { cross_block_bytes += sequence.matchLength; }
        if (sequence.rep != 0) { repeat_bytes += sequence.matchLength; }
        maximum_distance = std::max<std::size_t>(maximum_distance, sequence.offset);
        copied += sequence.matchLength;
        cursor += sequence.matchLength;
    }
    require(cursor == raw.size(), "diagnostic complete source");
    std::cout << "{\"kind\":\"" << kind << "\",\"raw_bytes\":" << raw.size()
              << ",\"zstd_bytes\":" << frame.size() << ",\"generated_sequences\":" << count
              << ",\"literal_bytes\":" << literals << ",\"copied_bytes\":" << copied
              << ",\"match_under_8_bytes\":" << short_bytes << ",\"distance_over_65535_bytes\":" << distant_bytes
              << ",\"cross_128k_bytes\":" << cross_block_bytes << ",\"repeat_distance_bytes\":" << repeat_bytes
              << ",\"maximum_distance\":" << maximum_distance << ",\"exact\":true}";
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "diagnostic frozen CSP path required");
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        require(static_cast<bool>(file), "diagnostic input open");
        std::streamoff length = file.tellg();
        require(length >= 48 && length <= 64 * 1024 * 1024, "diagnostic file bound");
        file.seekg(0);
        Bytes packet(static_cast<std::size_t>(length));
        file.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(packet.size()));
        require(static_cast<bool>(file), "diagnostic input read");
        constexpr std::array<std::uint8_t, 8> signature{137, 67, 83, 80, 13, 10, 26, 10};
        require(std::equal(signature.begin(), signature.end(), packet.begin()), "diagnostic signature");
        std::size_t position = 8;
        std::size_t frames = 0;
        std::cout << "{\"zstd_version\":\"" << ZSTD_versionString() << "\",\"frames\":[";
        while (position < packet.size()) {
            require(packet.size() - position >= 12, "diagnostic chunk");
            std::size_t size = big(packet, position);
            require(size <= packet.size() - position - 12, "diagnostic chunk extent");
            std::uint32_t kind = big(packet, position + 4);
            uLong checksum = crc32(0, packet.data() + position + 4, static_cast<uInt>(size + 4));
            require(checksum == big(packet, position + 8 + size), "diagnostic checksum");
            if (kind == 0x42414e44U) {
                require(size >= 13, "diagnostic band");
                std::span<const std::uint8_t> payload(packet.data() + position + 8, size);
                std::size_t sequence_size = big(payload, 8);
                require(sequence_size <= size - 13, "diagnostic sequence extent");
                if (frames != 0) { std::cout << ','; }
                inspect(payload.subspan(12, sequence_size), "sequences");
                ++frames;
                std::size_t literal = 12 + sequence_size;
                require(payload[literal] <= 1, "diagnostic literal coding");
                if (payload[literal] == 1) {
                    std::cout << ',';
                    inspect(payload.subspan(literal + 1), "literals");
                    ++frames;
                }
            }
            position += size + 12;
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
