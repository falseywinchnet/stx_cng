#include "phase_codec.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

// Command-line adapter for research/run_native_phase.py. Files are the only interface; exceptions stay here.
//   phase_driver encode W H C input.rgb output.cngphi
//   phase_driver decode input.cngphi output.rgb        (prints "W H C")
//   phase_driver check  W H C input.rgb expected.cngphi (encodes, compares bytes, decodes, compares samples)
//   phase_driver time   W H C input.rgb repeats        (in-process encode/decode repeats; prints min and median ns per pixel)
//   phase_driver trace  W H C input.rgb output.trace    (encodes; writes one 20-byte little-endian SymbolTrace record per symbol)
//   phase_driver frames W H C input.rgb repeats threads (encodes once; decodes all frames with that many threads, repeats times;
//                                                        prints min and median ns per pixel and the frame count)
namespace {
std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { throw std::runtime_error("cannot read " + path); }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return bytes;
}
void write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) { throw std::runtime_error("cannot write " + path); }
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
cng_phase::Dimensions parse_dimensions(char** argv) {
    cng_phase::Dimensions dimensions;
    dimensions.width = static_cast<std::uint32_t>(std::strtoul(argv[0], nullptr, 10));
    dimensions.height = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10));
    dimensions.channels = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10));
    return dimensions;
}
const char* status_name(cng_phase::Status status) {
    switch (status) {
        case cng_phase::Status::ok: return "ok";
        case cng_phase::Status::bad_dimensions: return "bad_dimensions";
        case cng_phase::Status::bad_sample_count: return "bad_sample_count";
        case cng_phase::Status::bad_header: return "bad_header";
        case cng_phase::Status::stream_exhausted: return "stream_exhausted";
        case cng_phase::Status::bad_directory: return "bad_directory";
        case cng_phase::Status::bad_checksum: return "bad_checksum";
    }
    return "unknown";
}
// Decodes frames worker, worker + stride, ... of `stream` into `restored`; records the first failure in `outcome`.
void decode_frame_set(const std::vector<std::uint8_t>& stream, std::vector<std::uint8_t>& restored, std::uint32_t frames, std::size_t worker, std::size_t stride, cng_phase::Status& outcome) {
    static_cast<void>(frames);
    outcome = cng_phase::decode_frame_range(stream, static_cast<std::uint32_t>(worker), static_cast<std::uint32_t>(stride), restored);
}
int run(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "encode" && argc == 7) {
        cng_phase::Dimensions dimensions = parse_dimensions(argv + 2);
        std::vector<std::uint8_t> samples = read_file(argv[5]);
        std::vector<std::uint8_t> stream;
        cng_phase::Status status = cng_phase::encode(samples, dimensions, stream);
        if (status != cng_phase::Status::ok) { std::cerr << status_name(status) << "\n"; return 2; }
        write_file(argv[6], stream);
        std::cout << stream.size() << "\n";
        return 0;
    }
    if (mode == "decode" && argc == 4) {
        std::vector<std::uint8_t> stream = read_file(argv[2]);
        cng_phase::Dimensions dimensions;
        cng_phase::Status header = cng_phase::read_header(stream, dimensions);
        if (header != cng_phase::Status::ok) { std::cerr << status_name(header) << "\n"; return 2; }
        std::vector<std::uint8_t> samples(static_cast<std::size_t>(dimensions.width) * dimensions.height * dimensions.channels);
        cng_phase::Status status = cng_phase::decode(stream, samples);
        if (status != cng_phase::Status::ok) { std::cerr << status_name(status) << "\n"; return 2; }
        write_file(argv[3], samples);
        std::cout << dimensions.width << " " << dimensions.height << " " << dimensions.channels << "\n";
        return 0;
    }
    if (mode == "check" && argc == 7) {
        cng_phase::Dimensions dimensions = parse_dimensions(argv + 2);
        std::vector<std::uint8_t> samples = read_file(argv[5]);
        std::vector<std::uint8_t> expected = read_file(argv[6]);
        std::vector<std::uint8_t> stream;
        cng_phase::Status status = cng_phase::encode(samples, dimensions, stream);
        if (status != cng_phase::Status::ok) { std::cerr << "encode " << status_name(status) << "\n"; return 2; }
        bool same_bytes = stream == expected;
        std::vector<std::uint8_t> restored(samples.size());
        cng_phase::Status decoded = cng_phase::decode(expected, restored);
        bool same_samples = decoded == cng_phase::Status::ok && restored == samples;
        std::cout << (same_bytes ? "bytes_match" : "bytes_differ") << " " << (same_samples ? "samples_match" : "samples_differ")
                  << " " << stream.size() << " " << expected.size() << "\n";
        return (same_bytes && same_samples) ? 0 : 1;
    }
    if (mode == "time" && argc == 7) {
        cng_phase::Dimensions dimensions = parse_dimensions(argv + 2);
        std::vector<std::uint8_t> samples = read_file(argv[5]);
        std::size_t repeats = static_cast<std::size_t>(std::strtoul(argv[6], nullptr, 10));
        if (repeats == 0) { repeats = 1; }
        std::vector<std::uint8_t> stream;
        std::vector<std::uint8_t> restored(samples.size());
        std::vector<double> encode_seconds(repeats);
        std::vector<double> decode_seconds(repeats);
        for (std::size_t trial = 0; trial < repeats; ++trial) {
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            cng_phase::Status status = cng_phase::encode(samples, dimensions, stream);
            std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
            if (status != cng_phase::Status::ok) { std::cerr << "encode " << status_name(status) << "\n"; return 2; }
            encode_seconds[trial] = std::chrono::duration<double>(stop - start).count();
            start = std::chrono::steady_clock::now();
            cng_phase::Status decoded = cng_phase::decode(stream, restored);
            stop = std::chrono::steady_clock::now();
            if (decoded != cng_phase::Status::ok || restored != samples) { std::cerr << "decode mismatch\n"; return 2; }
            decode_seconds[trial] = std::chrono::duration<double>(stop - start).count();
        }
        std::sort(encode_seconds.begin(), encode_seconds.end());
        std::sort(decode_seconds.begin(), decode_seconds.end());
        double pixels = static_cast<double>(dimensions.width) * static_cast<double>(dimensions.height);
        double scale = 1.0e9 / pixels;
        std::cout << "encode_ns_per_pixel_min " << encode_seconds.front() * scale
                  << " encode_ns_per_pixel_median " << encode_seconds[repeats / 2] * scale
                  << " decode_ns_per_pixel_min " << decode_seconds.front() * scale
                  << " decode_ns_per_pixel_median " << decode_seconds[repeats / 2] * scale
                  << " bytes " << stream.size() << " repeats " << repeats << "\n";
        return 0;
    }
    if (mode == "trace" && argc == 7) {
        cng_phase::Dimensions dimensions = parse_dimensions(argv + 2);
        std::vector<std::uint8_t> samples = read_file(argv[5]);
        std::vector<std::uint8_t> stream;
        std::vector<cng_phase::SymbolTrace> trace;
        cng_phase::Status status = cng_phase::encode_traced(samples, dimensions, stream, trace);
        if (status != cng_phase::Status::ok) { std::cerr << status_name(status) << "\n"; return 2; }
        std::vector<std::uint8_t> bytes(trace.size() * 20);
        for (std::size_t index = 0; index < trace.size(); ++index) {
            const cng_phase::SymbolTrace& record = trace[index];
            std::uint32_t fields[5] = {record.pixel, record.zero_context, record.coarse_context, record.sign_context, record.symbol};
            for (std::size_t field = 0; field < 5; ++field) {
                for (std::size_t byte = 0; byte < 4; ++byte) { bytes[index * 20 + field * 4 + byte] = static_cast<std::uint8_t>(fields[field] >> (8 * byte)); }
            }
        }
        write_file(argv[6], bytes);
        std::cout << stream.size() << " " << trace.size() << "\n";
        return 0;
    }
    if (mode == "encode-side" && argc == 8) {   // encode with a chosen frame side (research measurement of the frame size)
        cng_phase::Dimensions dimensions = parse_dimensions(argv + 2);
        std::vector<std::uint8_t> samples = read_file(argv[5]);
        std::uint32_t side = static_cast<std::uint32_t>(std::strtoul(argv[7], nullptr, 10));
        std::vector<std::uint8_t> stream;
        cng_phase::Status status = cng_phase::encode_with_frame(samples, dimensions, side, stream);
        if (status != cng_phase::Status::ok) { std::cerr << status_name(status) << "\n"; return 2; }
        write_file(argv[6], stream);
        std::cout << stream.size() << "\n";
        return 0;
    }
    if (mode == "salvage" && argc == 5) {   // decode every intact frame, fill the lost ones from the mosaic; write a per-frame mask (1 = decoded)
        std::vector<std::uint8_t> stream = read_file(argv[2]);
        cng_phase::Dimensions dimensions;
        cng_phase::Status header = cng_phase::read_header(stream, dimensions);
        if (header != cng_phase::Status::ok) { std::cerr << status_name(header) << "\n"; return 3; }
        std::vector<std::uint8_t> samples(static_cast<std::size_t>(dimensions.width) * dimensions.height * dimensions.channels, 0);
        std::vector<std::uint8_t> mask;
        bool mosaic_ok = false;
        cng_phase::Status status = cng_phase::decode_salvage(stream, samples, mask, mosaic_ok);
        if (status != cng_phase::Status::ok) { std::cerr << status_name(status) << "\n"; return 3; }
        std::uint32_t decoded = 0;
        for (std::uint8_t ok : mask) { decoded += ok; }
        write_file(argv[3], samples);
        write_file(argv[4], mask);
        std::cout << dimensions.width << " " << dimensions.height << " " << dimensions.channels << " " << mask.size() << " " << decoded << " mosaic " << (mosaic_ok ? 1 : 0) << "\n";
        return 0;
    }
    if (mode == "mosaic" && argc == 4) {   // decode the mosaic layer alone
        std::vector<std::uint8_t> stream = read_file(argv[2]);
        cng_phase::Dimensions dimensions;
        cng_phase::Status header = cng_phase::read_header(stream, dimensions);
        if (header != cng_phase::Status::ok) { std::cerr << status_name(header) << "\n"; return 2; }
        cng_phase::Dimensions m = cng_phase::mosaic_dimensions(dimensions);
        std::vector<std::uint8_t> samples(static_cast<std::size_t>(m.width) * m.height * m.channels);
        cng_phase::Status status = cng_phase::decode_mosaic(stream, samples);
        if (status != cng_phase::Status::ok) { std::cerr << status_name(status) << "\n"; return 2; }
        write_file(argv[3], samples);
        std::cout << m.width << " " << m.height << " " << m.channels << "\n";
        return 0;
    }
    if (mode == "frames" && argc == 8) {
        cng_phase::Dimensions dimensions = parse_dimensions(argv + 2);
        std::vector<std::uint8_t> samples = read_file(argv[5]);
        std::size_t repeats = static_cast<std::size_t>(std::strtoul(argv[6], nullptr, 10));
        std::size_t threads = static_cast<std::size_t>(std::strtoul(argv[7], nullptr, 10));
        if (repeats == 0) { repeats = 1; }
        if (threads == 0) { threads = 1; }
        std::vector<std::uint8_t> stream;
        cng_phase::Status status = cng_phase::encode(samples, dimensions, stream);
        if (status != cng_phase::Status::ok) { std::cerr << "encode " << status_name(status) << "\n"; return 2; }
        const std::uint32_t frames = cng_phase::frame_count(stream, dimensions);
        std::vector<std::uint8_t> restored(samples.size());
        std::vector<double> decode_seconds(repeats);
        for (std::size_t trial = 0; trial < repeats; ++trial) {
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            // Each worker decodes the frames index = worker, worker + threads, ... into disjoint rectangles of `restored`.
            std::vector<std::thread> workers;
            std::vector<cng_phase::Status> outcomes(threads, cng_phase::Status::ok);
            for (std::size_t worker = 0; worker < threads; ++worker) {
                workers.emplace_back(decode_frame_set, std::cref(stream), std::ref(restored), frames, worker, threads, std::ref(outcomes[worker]));
            }
            for (std::thread& worker : workers) { worker.join(); }
            std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
            for (cng_phase::Status outcome : outcomes) { if (outcome != cng_phase::Status::ok) { std::cerr << "decode " << status_name(outcome) << "\n"; return 2; } }
            if (restored != samples) { std::cerr << "decode mismatch\n"; return 2; }
            decode_seconds[trial] = std::chrono::duration<double>(stop - start).count();
        }
        std::sort(decode_seconds.begin(), decode_seconds.end());
        double pixels = static_cast<double>(dimensions.width) * static_cast<double>(dimensions.height);
        double scale = 1.0e9 / pixels;
        std::cout << "frames " << frames << " threads " << threads << " decode_ns_per_pixel_min " << decode_seconds.front() * scale
                  << " decode_ns_per_pixel_median " << decode_seconds[repeats / 2] * scale << " bytes " << stream.size() << "\n";
        return 0;
    }
    std::cerr << "usage: phase_driver encode W H C in.rgb out.cngphi | decode in.cngphi out.rgb | check W H C in.rgb expected.cngphi | time W H C in.rgb repeats | trace W H C in.rgb out.trace | frames W H C in.rgb repeats threads | encode-side W H C in.rgb out.cngphi side | salvage in.cngphi out.rgb out.mask | mosaic in.cngphi out.rgb\n";
    return 64;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 70;
    }
}
