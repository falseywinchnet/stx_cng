#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace cng_packet_polish {
using Lengths = std::array<std::uint8_t, 256>;
// Counts are for actual contiguous coding lanes, not interleaved counters.
using Counts = std::array<std::array<std::uint32_t, 256>, 4>;
using Bits = std::array<std::int64_t, 4>;
unsigned run_cost(unsigned start, unsigned value, unsigned count) {
    unsigned lead = start == 0 && value == 0 ? 0U : 1U;
    unsigned repeats = count - lead;
    unsigned cost = repeats;
    if (repeats >= 19) { cost = 3; }
    else if (repeats >= 3) { cost = 2; }
    cost += lead;
    return cost;
}
struct Run {
    std::uint16_t begin = 0;
    std::uint16_t end = 0;
    std::uint8_t value = 0;
};
struct Map {
    std::array<Run, 256> runs{};
    std::array<std::uint16_t, 256> ids{};
    unsigned size = 0;
    unsigned nibbles = 0;
    explicit Map(const Lengths& lengths) {
        for (unsigned position = 0; position < 256;) {
            unsigned end = position + 1;
            while (end < 256 && lengths[end] == lengths[position]) { ++end; }
            runs[size] = Run{static_cast<std::uint16_t>(position), static_cast<std::uint16_t>(end), lengths[position]};
            for (unsigned index = position; index < end; ++index) { ids[index] = static_cast<std::uint16_t>(size); }
            nibbles += run_cost(position, lengths[position], end - position);
            ++size;
            position = end;
        }
    }
};
struct Patch {
    unsigned size = 0;
    std::array<std::uint16_t, 3> at{}; // Live positions are sorted and distinct.
    std::array<std::uint8_t, 3> value{};
};
struct Segments {
    unsigned live = 0;
    unsigned value = 0;
    unsigned start = 0;
    int cost = 0;
    void append(unsigned position, unsigned next, unsigned count) {
        if (count == 0) { return; }
        if (live != 0 && value == next) { live += count; return; }
        if (live != 0) { cost += static_cast<int>(run_cost(start, value, live)); }
        start = position;
        value = next;
        live = count;
    }
};
// Borrow a fixed current map; inspect at most nine runs around three edits.
// Unchanged neighboring runs close each window against any further merge.
int nibble_delta(const Map& map, const Patch& patch) {
    std::array<unsigned, 3> low{};
    std::array<unsigned, 3> high{};
    unsigned windows = 0;
    for (unsigned index = 0; index < patch.size; ++index) {
        unsigned id = map.ids[patch.at[index]];
        unsigned first = id == 0 ? 0 : id - 1;
        unsigned last = std::min(id + 1, map.size - 1);
        if (windows != 0 && first <= high[windows - 1] + 1) {
            high[windows - 1] = std::max(high[windows - 1], last);
        } else {
            low[windows] = first;
            high[windows] = last;
            ++windows;
        }
    }
    int delta = 0;
    for (unsigned window = 0; window < windows; ++window) {
        Segments segments{};
        for (unsigned index = low[window]; index <= high[window]; ++index) {
            const Run& old = map.runs[index];
            delta -= static_cast<int>(run_cost(old.begin, old.value, old.end - old.begin));
            unsigned position = old.begin;
            for (unsigned edit = 0; edit < patch.size; ++edit) {
                unsigned at = patch.at[edit];
                if (at < old.begin || at >= old.end) { continue; }
                segments.append(position, old.value, at - position);
                segments.append(at, patch.value[edit], 1);
                position = at + 1;
            }
            segments.append(position, old.value, old.end - position);
        }
        if (segments.live != 0) {
            segments.cost += static_cast<int>(run_cost(segments.start, segments.value, segments.live));
        }
        delta += segments.cost;
    }
    return delta;
}
Bits lane_bits(const Counts& counts, const Lengths& lengths) {
    Bits bits{};
    for (unsigned lane = 0; lane < 4; ++lane) {
        for (unsigned symbol = 0; symbol < 256; ++symbol) {
            bits[lane] += static_cast<std::int64_t>(counts[lane][symbol]) * lengths[symbol];
        }
    }
    return bits;
}
std::uint64_t bytes(unsigned nibbles, const Bits& bits) {
    std::uint64_t total = (nibbles + 1) / 2;
    for (std::int64_t count : bits) { total += static_cast<std::uint64_t>(count + 7) / 8; }
    return total;
}
std::uint64_t score(const Counts& counts, const Lengths& lengths) {
    Map map(lengths);
    Bits bits = lane_bits(counts, lengths);
    std::uint64_t result = bytes(map.nibbles, bits);
    return result;
}
std::uint64_t patch_score(const Counts& counts, const Lengths& lengths, const Map& map,
                          const Bits& bits, const Patch& patch) {
    Bits changed = bits;
    for (unsigned edit = 0; edit < patch.size; ++edit) {
        unsigned symbol = patch.at[edit];
        int difference = static_cast<int>(patch.value[edit]) - static_cast<int>(lengths[symbol]);
        for (unsigned lane = 0; lane < 4; ++lane) {
            changed[lane] += static_cast<std::int64_t>(counts[lane][symbol]) * difference;
        }
    }
    int nibbles = static_cast<int>(map.nibbles) + nibble_delta(map, patch);
    std::uint64_t result = bytes(static_cast<unsigned>(nibbles), changed);
    return result;
}
struct Result {
    std::uint64_t before = 0;
    std::uint64_t after = 0;
    std::uint64_t proposals = 0;
    unsigned accepted = 0;
    bool local_optimum = false;
};
void consider(const Counts& counts, const Lengths& lengths, const Map& map, const Bits& bits,
              const Patch& patch, Patch& best, std::uint64_t& best_cost, Result& result) {
    bool changed = false;
    for (unsigned edit = 0; edit < patch.size; ++edit) {
        changed |= lengths[patch.at[edit]] != patch.value[edit];
    }
    if (!changed) { return; }
    ++result.proposals;
    std::uint64_t cost = patch_score(counts, lengths, map, bits, patch);
    if (cost < best_cost) { best = patch; best_cost = cost; }
}
// In-place lengths; counts must describe <=2 MiB, the same fixed support and
// a complete 1..12-bit code. No allocation or retained borrow. The caller owns
// counts and lengths separately. Each accepted edit strictly reduces bytes.
Result improve(const Counts& counts, Lengths& lengths, unsigned rounds) {
    std::array<std::uint16_t, 256> active{};
    unsigned count = 0;
    for (unsigned symbol = 0; symbol < 256; ++symbol) {
        if (lengths[symbol] == 0) { continue; }
        active[count] = static_cast<std::uint16_t>(symbol);
        ++count;
    }
    Result result{};
    result.before = score(counts, lengths);
    result.after = result.before;
    for (unsigned round = 0; round < rounds; ++round) {
        Map map(lengths);
        Bits bits = lane_bits(counts, lengths);
        Patch best{};
        std::uint64_t best_cost = result.after;
        for (unsigned index = 0; index + 1 < count; ++index) {
            Patch patch{2, {active[index], active[index + 1], 0},
                        {lengths[active[index + 1]], lengths[active[index]], 0}};
            consider(counts, lengths, map, bits, patch, best, best_cost, result);
        }
        for (unsigned index = 0; index + 2 < count; ++index) {
            Patch patch{3, {active[index], active[index + 1], active[index + 2]}, {}};
            std::array<unsigned, 3> old{lengths[patch.at[0]], lengths[patch.at[1]], lengths[patch.at[2]]};
            for (unsigned anchor = 0; anchor < 3; ++anchor) {
                if (anchor != 0 && old[0] == old[1] && old[1] == old[2]) { continue; }
                unsigned h = old[anchor];
                unsigned k = old[(anchor + 1) % 3];
                if (k != old[(anchor + 2) % 3] || h >= 12 || k < 2) { continue; }
                // Splitting one leaf and merging two preserves Kraft equality.
                for (unsigned short_at = 0; short_at < 3; ++short_at) {
                    if (short_at != 0 && h + 1 == k - 1) { continue; }
                    patch.value.fill(static_cast<std::uint8_t>(h + 1));
                    patch.value[short_at] = static_cast<std::uint8_t>(k - 1);
                    consider(counts, lengths, map, bits, patch, best, best_cost, result);
                }
            }
        }
        if (best.size == 0) { result.local_optimum = true; break; }
        for (unsigned edit = 0; edit < best.size; ++edit) { lengths[best.at[edit]] = best.value[edit]; }
        result.after = best_cost;
        ++result.accepted;
    }
    return result;
}
}
