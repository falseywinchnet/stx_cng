// Diagnostic inclusion keeps baseline match discovery and model fitting exact.
#include "../whole_codec/match_bounded.cpp"
#include "../entropy_reduction/huffman_reduced.cpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include "count.hpp"

namespace {
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
struct Removal {
    std::size_t token_start = 0;
    std::size_t token_end = 0;
    std::size_t source_start = 0;
    std::size_t source_count = 0;
    std::size_t match_length = 0;
};
struct Workspace {
    cng_match_trial::EncodeWorkspace encoder;
    cng_match_trial::DecodeWorkspace decoder;
    std::array<std::uint8_t, cng_match_trial::block_size + 64> alternative;
    std::array<std::uint8_t, cng_match_trial::block_size + 64> selected;
    std::array<Removal, 64> removals;
};
// Replacing one match also merges its literals with the following literal run.
// Only the first 64 matches of length 8..32 are considered. Discovery is frozen.
std::size_t discover(std::span<const std::uint8_t> tokens, std::size_t begin,
                     std::size_t count, Workspace& workspace, std::size_t& matches) {
    std::size_t read = 0;
    std::size_t position = begin;
    std::size_t end = begin + count;
    std::size_t found = 0;
    matches = 0;
    while (position < end) {
        std::size_t token_start = read;
        std::size_t source_start = position;
        std::size_t literals = cng_match_trial::integer(tokens, read);
        require(literals <= tokens.size() - read && literals <= end - position, "baseline literal extent");
        read += literals;
        position += literals;
        if (position == end) { break; }
        std::size_t distance = cng_match_trial::integer(tokens, read);
        std::size_t length = cng_match_trial::integer(tokens, read) + 8;
        require(distance != 0 && distance <= position && length <= end - position, "baseline match extent");
        position += length;
        ++matches;
        if (length > 32 || found == workspace.removals.size()) { continue; }
        std::size_t tail = read;
        std::size_t following = 0;
        if (position != end) {
            following = cng_match_trial::integer(tokens, tail);
            require(following <= tokens.size() - tail && following <= end - position, "following literals");
            tail += following;
        }
        workspace.removals[found] = Removal{token_start, tail, source_start, literals + length + following, length};
        ++found;
    }
    require(read == tokens.size(), "baseline token consumption");
    return found;
}
std::size_t remove_match(std::span<const std::uint8_t> tokens, std::span<const std::uint8_t> raw,
                         const Removal& removal, std::span<std::uint8_t> output) {
    std::size_t written = removal.token_start;
    std::copy_n(tokens.data(), written, output.data());
    cng_match_trial::integer(output, written, removal.source_count);
    require(removal.source_count <= output.size() - written, "replacement literal capacity");
    std::copy_n(raw.data() + removal.source_start, removal.source_count, output.data() + written);
    written += removal.source_count;
    std::size_t tail = tokens.size() - removal.token_end;
    require(tail <= output.size() - written, "replacement token capacity");
    std::copy_n(tokens.data() + removal.token_end, tail, output.data() + written);
    written += tail;
    return written;
}
void write_frame(std::ostream& stream, std::span<const std::uint8_t> packet) {
    std::array<std::uint8_t, 4> header{};
    cng_match_trial::put32(header, 0, static_cast<std::uint32_t>(packet.size()));
    stream.write(reinterpret_cast<const char*>(header.data()), 4);
    stream.write(reinterpret_cast<const char*>(packet.data()), static_cast<std::streamsize>(packet.size()));
    require(static_cast<bool>(stream), "selected frame write");
}
void examine(std::span<const std::uint8_t> packet, std::size_t frame, Workspace& workspace,
             std::ostream& rows, std::ostream& frames) {
    std::size_t size = cng_match_trial::get32(packet, 1);
    require(size <= cng_entropy_trial::maximum_input, "frame bound");
    std::vector<std::uint8_t> raw(size);
    std::vector<std::uint8_t> reconstructed(size);
    std::vector<std::uint8_t> baseline(size + 4 * ((size + 131071) / 131072) + 5);
    std::vector<std::uint8_t> selected(baseline.size());
    cng_match_trial::decode(packet, raw, workspace.decoder);
    Clock::time_point start = Clock::now();
    std::size_t baseline_size = cng_match_trial::encode(raw, baseline, workspace.encoder);
    double baseline_seconds = std::chrono::duration<double>(Clock::now() - start).count();
    require(baseline_size == packet.size() && std::equal(packet.begin(), packet.end(), baseline.begin()), "baseline identity");
    reconstructed = raw;
    std::size_t slots = std::bit_floor(std::min<std::size_t>(16384, std::max<std::size_t>(16, size / 4)));
    std::fill_n(workspace.encoder.index.data(), slots, cng_match_trial::Bucket{});
    selected[0] = 16;
    cng_match_trial::put32(selected, 1, static_cast<std::uint32_t>(size));
    std::size_t written = 5;
    std::size_t saved = 0;
    for (std::size_t offset = 0; offset < size;) {
        std::size_t count = std::min(cng_match_trial::block_size, size - offset);
        cng_match_trial::ParseResult parsed = cng_match_trial::parse(raw, offset, count, slots - 1, workspace.encoder);
        std::span<const std::uint8_t> tokens = parsed.references
            ? std::span<const std::uint8_t>(workspace.encoder.parsed).first(parsed.size)
            : std::span<const std::uint8_t>(raw).subspan(offset, count);
        std::size_t prefix = parsed.references ? 4 : 0;
        std::size_t old_body = std::min(count, prefix + huffman_size(tokens));
        std::size_t best_body = old_body;
        std::size_t best_size = tokens.size();
        std::size_t best_length = 0;
        std::size_t proposals = 0;
        std::size_t matches = 0;
        std::size_t improving = 0;
        start = Clock::now();
        if (parsed.references) {
            proposals = discover(tokens, offset, count, workspace, matches);
            for (std::size_t proposal = 0; proposal < proposals; ++proposal) {
                const Removal& removal = workspace.removals[proposal];
                std::size_t length = remove_match(tokens, raw, removal, workspace.alternative);
                std::span<const std::uint8_t> candidate = std::span<const std::uint8_t>(workspace.alternative).first(length);
                // Every proposed representation is checked, including rejected edits.
                cng_match_trial::reconstruct(candidate, reconstructed, offset, count);
                require(std::equal(raw.begin() + offset, raw.begin() + offset + count, reconstructed.begin() + offset), "removal exactness");
                std::size_t body = 4 + huffman_size(candidate);
                if (body < old_body) { ++improving; }
                if (body >= best_body) { continue; }
                best_body = body;
                best_size = length;
                best_length = removal.match_length;
                std::copy(candidate.begin(), candidate.end(), workspace.selected.begin());
            }
        }
        double search_seconds = std::chrono::duration<double>(Clock::now() - start).count();
        if (best_length != 0) { tokens = std::span<const std::uint8_t>(workspace.selected).first(best_size); }
        if (best_body == count) {
            cng_match_trial::put32(selected, written, 0x80000000U | static_cast<std::uint32_t>(count));
            std::copy_n(raw.data() + offset, count, selected.data() + written + 4);
        } else {
            std::size_t coded = cng_huffman_kernel::encode(tokens, workspace.encoder.coded);
            require(coded + prefix == best_body, "selected exact count");
            std::uint32_t header = static_cast<std::uint32_t>(best_body);
            if (!parsed.references) { header |= 0x40000000U; }
            cng_match_trial::put32(selected, written, header);
            if (parsed.references) { cng_match_trial::put32(selected, written + 4, static_cast<std::uint32_t>(tokens.size())); }
            std::copy_n(workspace.encoder.coded.data(), coded, selected.data() + written + 4 + prefix);
        }
        saved += old_body - best_body;
        written += 4 + best_body;
        rows << "block," << frame << ',' << offset << ',' << count << ',' << matches << ',' << proposals << ','
             << improving << ',' << old_body << ',' << best_body << ',' << best_length << ',' << search_seconds << '\n';
        offset += count;
    }
    if (written >= size + 5) {
        selected[0] = 0;
        std::copy(raw.begin(), raw.end(), selected.begin() + 5);
        written = size + 5;
    }
    require(written <= baseline_size, "complete frame size gate");
    std::span<const std::uint8_t> selected_packet = std::span<const std::uint8_t>(selected).first(written);
    cng_match_trial::decode(selected_packet, reconstructed, workspace.decoder);
    require(raw == reconstructed, "selected full frame inverse");
    write_frame(frames, selected_packet);
    rows << "frame," << frame << ",0," << size << ",0,0,0," << baseline_size << ',' << written << ',' << saved << ',' << baseline_seconds << '\n';
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 4, "input bundle, CSV and selected bundle required");
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        std::ofstream frames(argv[3], std::ios::binary);
        require(static_cast<bool>(input) && static_cast<bool>(rows) && static_cast<bool>(frames), "study files open");
        rows.precision(12);
        rows << "kind,frame,offset,raw_bytes,matches,proposals,improving,baseline_bytes,selected_bytes,detail,seconds\n";
        std::unique_ptr<Workspace> workspace = std::make_unique<Workspace>();
        std::size_t frame = 0;
        while (input.peek() != std::char_traits<char>::eof()) {
            std::array<std::uint8_t, 4> header{};
            input.read(reinterpret_cast<char*>(header.data()), 4);
            require(static_cast<bool>(input), "bundle header");
            std::size_t size = cng_match_trial::get32(header, 0);
            require(size >= 5 && size <= cng_entropy_trial::maximum_input + 5, "bundle frame size");
            std::vector<std::uint8_t> packet(size);
            input.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(size));
            require(static_cast<bool>(input), "bundle frame read");
            examine(packet, frame, *workspace, rows, frames);
            ++frame;
        }
        rows.close();
        frames.close();
        require(static_cast<bool>(rows) && static_cast<bool>(frames), "study output complete");
        std::cout << "PASS: " << frame << " baseline frame identities, bounded alternatives and selected inverses\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
