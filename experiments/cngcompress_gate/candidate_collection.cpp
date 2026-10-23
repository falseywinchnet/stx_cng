#include "candidate_collection.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <streambuf>
#include <zlib.h>

namespace cng_candidate_collection {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t maximum_images = 4096;
constexpr std::array<std::uint8_t, 8> signature{137, 67, 78, 88, 13, 10, 26, 10};
using Tag = std::array<std::uint8_t, 4>;
constexpr Tag image_tag{73, 77, 65, 71};
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
void write(std::ostream& stream, std::span<const std::uint8_t> bytes) {
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(stream), "atlas write");
}
void read(std::istream& stream, std::span<std::uint8_t> bytes) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(stream), "atlas read");
}
void put(std::span<std::uint8_t> bytes, std::size_t offset, std::size_t width, std::uint64_t value) {
    for (std::size_t index = 0; index < width; ++index) {
        bytes[offset + width - index - 1] = static_cast<std::uint8_t>(value);
        value >>= 8;
    }
    require(value == 0, "atlas integer extent");
}
std::uint64_t get(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) { value = (value << 8) | bytes[offset + index]; }
    return value;
}
uLong checksum(Tag tag, std::span<const std::uint8_t> bytes) {
    uLong result = crc32(0, tag.data(), 4);
    if (!bytes.empty()) { result = crc32(result, bytes.data(), static_cast<uInt>(bytes.size())); }
    return result;
}
void chunk(std::ostream& stream, Tag tag, std::span<const std::uint8_t> bytes) {
    require(bytes.size() <= std::numeric_limits<std::uint32_t>::max(), "atlas chunk extent");
    std::array<std::uint8_t, 8> header{};
    put(header, 0, 4, bytes.size());
    std::copy(tag.begin(), tag.end(), header.begin() + 4);
    write(stream, header);
    write(stream, bytes);
    std::array<std::uint8_t, 4> footer{};
    put(footer, 0, 4, checksum(tag, bytes));
    write(stream, footer);
}
Bytes read_chunk(std::istream& stream, Tag tag, std::size_t bound) {
    std::array<std::uint8_t, 8> header{};
    read(stream, header);
    require(std::equal(tag.begin(), tag.end(), header.begin() + 4), "atlas chunk type");
    std::size_t length = static_cast<std::size_t>(get(header, 0, 4));
    require(length <= bound, "atlas chunk bound");
    Bytes bytes(length);
    read(stream, bytes);
    std::array<std::uint8_t, 4> footer{};
    read(stream, footer);
    require(get(footer, 0, 4) == checksum(tag, bytes), "atlas chunk checksum");
    return bytes;
}
std::uint64_t position(std::ostream& stream) {
    std::streampos current = stream.tellp();
    require(current >= 0, "atlas seekable output");
    std::uint64_t result = static_cast<std::uint64_t>(current);
    return result;
}
void seek(std::istream& stream, std::uint64_t offset) {
    require(offset <= static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()), "atlas offset");
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    require(static_cast<bool>(stream), "atlas seekable input");
}
// Forward each codec write while accumulating a single outer chunk checksum.
// The underlying output is borrowed; no body is retained or reread.
class ChunkOutput : public std::streambuf {
    std::ostream& output;
public:
    std::uint64_t length = 0;
    uLong crc = crc32(0, image_tag.data(), 4);
    explicit ChunkOutput(std::ostream& destination) : output(destination) {}
    std::streamsize xsputn(const char* data, std::streamsize count) override {
        if (count < 0 || static_cast<std::uint64_t>(count) > std::numeric_limits<std::uint32_t>::max() - length) { return 0; }
        output.write(data, count);
        if (!output) { return 0; }
        crc = crc32(crc, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(count));
        length += static_cast<std::uint64_t>(count);
        return count;
    }
    int_type overflow(int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof())) { return traits_type::not_eof(value); }
        char byte = traits_type::to_char_type(value);
        std::streamsize written = xsputn(&byte, 1);
        if (written != 1) { return traits_type::eof(); }
        return value;
    }
};
// Bounded input makes the inner codec's EOF local to this one image.
class ChunkInput : public std::streambuf {
    std::istream& input;
    std::array<char, 4096> buffer{};
    std::uint64_t remaining;
public:
    uLong crc = crc32(0, image_tag.data(), 4);
    ChunkInput(std::istream& source, std::uint64_t length) : input(source), remaining(length) {}
    int_type underflow() override {
        if (gptr() != nullptr && gptr() < egptr()) { return traits_type::to_int_type(*gptr()); }
        if (remaining == 0) { return traits_type::eof(); }
        std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        input.read(buffer.data(), static_cast<std::streamsize>(count));
        if (!input) { return traits_type::eof(); }
        crc = crc32(crc, reinterpret_cast<const Bytef*>(buffer.data()), static_cast<uInt>(count));
        remaining -= count;
        setg(buffer.data(), buffer.data(), buffer.data() + count);
        return traits_type::to_int_type(*gptr());
    }
};
void validate_dimensions(Dimensions dimensions) {
    require(dimensions.height > 0 && dimensions.width > 0
        && dimensions.height <= 65536 && dimensions.width <= 65536
        && (dimensions.channels == 3 || dimensions.channels == 4), "collection dimensions");
    std::size_t padded_height = ((dimensions.height + 127) / 128) * 128;
    std::size_t padded_width = ((dimensions.width + 127) / 128) * 128;
    require(padded_height <= 4194304 / padded_width && padded_height + padded_width <= 65536,
        "collection canvas bound");
}
Layout plan(std::span<const ImageSource> images, Form form) {
    require(!images.empty() && images.size() <= maximum_images, "collection count");
    require(form == Form::packed || form == Form::composite, "collection form");
    Layout layout;
    layout.form = form;
    layout.entries.resize(images.size());
    for (std::size_t index = 0; index < images.size(); ++index) {
        const ImageSource& image = images[index];
        validate_dimensions(image.dimensions);
        require(std::filesystem::file_size(image.raw_path) == image.dimensions.height * image.dimensions.width * image.dimensions.channels,
            "source byte extent");
        for (std::size_t prior = 0; prior < index; ++prior) {
            require(images[prior].id.value != image.id.value, "duplicate collection id");
        }
        Entry& entry = layout.entries[index];
        entry.id = image.id;
        entry.dimensions = image.dimensions;
        if (form == Form::composite) {
            require(image.dimensions.height <= 65536 - layout.canvas.height, "composite height");
            entry.y = layout.canvas.height;
            layout.canvas.height += image.dimensions.height;
            layout.canvas.width = std::max(layout.canvas.width, image.dimensions.width);
            layout.canvas.channels = std::max(layout.canvas.channels, image.dimensions.channels);
        }
    }
    if (form == Form::composite) { validate_dimensions(layout.canvas); }
    return layout;
}
void pack_entries(std::span<const Entry> entries, std::span<std::uint8_t> bytes) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const Entry& entry = entries[index];
        std::size_t offset = index * 40;
        put(bytes, offset, 4, entry.id.value);
        put(bytes, offset + 4, 4, entry.dimensions.width);
        put(bytes, offset + 8, 4, entry.dimensions.height);
        put(bytes, offset + 12, 4, entry.dimensions.channels);
        put(bytes, offset + 16, 4, entry.x);
        put(bytes, offset + 20, 4, entry.y);
        put(bytes, offset + 24, 8, entry.offset.bytes);
        put(bytes, offset + 32, 8, entry.length);
    }
}
// Borrow immutable image descriptions and keep at most one source file open.
// Row buffers own their bytes until the consumer has exhausted the get area.
class CompositeInput : public std::streambuf {
    std::span<const ImageSource> images;
    Dimensions canvas;
    std::ifstream input;
    std::vector<char> row;
    std::vector<char> original;
    std::size_t image_index = 0;
    std::size_t image_row = 0;
public:
    CompositeInput(std::span<const ImageSource> sources, Dimensions dimensions)
        : images(sources), canvas(dimensions), input{},
          row(dimensions.width * dimensions.channels), original(dimensions.width * 4) {}
    std::size_t workspace_bytes() const {
        std::size_t result = row.capacity() + original.capacity();
        return result;
    }
    int_type underflow() override {
        if (gptr() != nullptr && gptr() < egptr()) { return traits_type::to_int_type(*gptr()); }
        if (image_index == images.size()) { return traits_type::eof(); }
        const ImageSource& image = images[image_index];
        if (image_row == 0) {
            input.open(image.raw_path, std::ios::binary);
            require(static_cast<bool>(input), "composite source open");
        }
        std::size_t count = image.dimensions.width * image.dimensions.channels;
        std::fill(row.begin(), row.end(), 0);
        if (image.dimensions.channels == canvas.channels) {
            input.read(row.data(), static_cast<std::streamsize>(count));
        } else {
            input.read(original.data(), static_cast<std::streamsize>(count));
            for (std::size_t x = 0; x < image.dimensions.width; ++x) {
                row[x * 4] = original[x * 3];
                row[x * 4 + 1] = original[x * 3 + 1];
                row[x * 4 + 2] = original[x * 3 + 2];
                row[x * 4 + 3] = static_cast<char>(255);
            }
        }
        require(static_cast<bool>(input), "composite source row");
        ++image_row;
        if (image_row == image.dimensions.height) {
            require(input.peek() == std::char_traits<char>::eof() && !input.bad(), "composite trailing source");
            input.close();
            input.clear();
            image_row = 0;
            ++image_index;
        }
        setg(row.data(), row.data(), row.data() + row.size());
        return traits_type::to_int_type(*gptr());
    }
};
void encode_image(std::istream& input, Dimensions dimensions, std::ostream& output,
                  Entry& entry, Measurements& measurements) {
    entry.offset.bytes = position(output);
    std::array<std::uint8_t, 8> header{};
    std::copy(image_tag.begin(), image_tag.end(), header.begin() + 4);
    write(output, header);
    ChunkOutput buffer(output);
    std::ostream body(&buffer);
    cng_candidate::Measurements measured;
    cng_candidate::encode(input, body, dimensions, measured);
    measurements.workspace_bytes = std::max(measurements.workspace_bytes, measured.workspace_bytes + sizeof(buffer));
    std::array<std::uint8_t, 4> footer{};
    put(footer, 0, 4, buffer.crc);
    write(output, footer);
    entry.length = buffer.length + 12;
    std::uint64_t end = position(output);
    output.seekp(static_cast<std::streamoff>(entry.offset.bytes));
    put(header, 0, 4, buffer.length);
    write(output, header);
    output.seekp(static_cast<std::streamoff>(end));
    require(static_cast<bool>(output), "collection image patch");
}
void decode_image(std::istream& input, const Entry& entry, Dimensions dimensions,
                  Bytes& pixels, Measurements& measurements) {
    seek(input, entry.offset.bytes);
    std::array<std::uint8_t, 8> header{};
    read(input, header);
    require(std::equal(image_tag.begin(), image_tag.end(), header.begin() + 4)
        && get(header, 0, 4) == entry.length - 12, "collection image header");
    ChunkInput buffer(input, entry.length - 12);
    std::istream body(&buffer);
    pixels.resize(dimensions.height * dimensions.width * dimensions.channels);
    cng_candidate::Measurements measured;
    Dimensions actual = cng_candidate::decode_into(body, pixels, measured);
    require(actual.width == dimensions.width && actual.height == dimensions.height
        && actual.channels == dimensions.channels, "collection inner dimensions");
    std::array<std::uint8_t, 4> footer{};
    read(input, footer);
    require(get(footer, 0, 4) == buffer.crc, "collection image CRC");
    measurements.workspace_bytes = std::max(measurements.workspace_bytes, measured.workspace_bytes + sizeof(buffer));
}
}

void encode(std::span<const ImageSource> images, Form form,
            std::ostream& destination, Measurements& measurements) {
    measurements = {};
    Layout layout = plan(images, form);
    require(position(destination) == 0, "collection output origin");
    write(destination, signature);
    std::array<std::uint8_t, 24> header{};
    put(header, 0, 4, 0); // Experimental collection schema, distinct from AHDR atlases.
    put(header, 4, 4, static_cast<std::uint32_t>(form));
    put(header, 8, 4, images.size());
    put(header, 12, 4, layout.canvas.width);
    put(header, 16, 4, layout.canvas.height);
    put(header, 20, 4, layout.canvas.channels);
    chunk(destination, {83, 72, 68, 82}, header);
    std::uint64_t directory_position = position(destination);
    Bytes packed(images.size() * 40);
    chunk(destination, {68, 73, 82, 69}, packed);
    if (form == Form::packed) {
        for (std::size_t index = 0; index < images.size(); ++index) {
            std::ifstream input(images[index].raw_path, std::ios::binary);
            require(static_cast<bool>(input), "packed source open");
            encode_image(input, images[index].dimensions, destination, layout.entries[index], measurements);
        }
    } else {
        CompositeInput buffer(images, layout.canvas);
        std::istream input(&buffer);
        Entry canvas;
        encode_image(input, layout.canvas, destination, canvas, measurements);
        measurements.workspace_bytes += buffer.workspace_bytes() + sizeof(buffer);
        for (Entry& entry : layout.entries) {
            entry.offset = canvas.offset;
            entry.length = canvas.length;
        }
    }
    chunk(destination, {83, 69, 78, 68}, {});
    std::uint64_t end = position(destination);
    pack_entries(layout.entries, packed);
    destination.seekp(static_cast<std::streamoff>(directory_position));
    chunk(destination, {68, 73, 82, 69}, packed);
    destination.seekp(static_cast<std::streamoff>(end));
    require(static_cast<bool>(destination), "collection final patch");
    measurements.workspace_bytes += packed.capacity() + layout.entries.capacity() * sizeof(Entry);
}

void decode(std::istream& source, Decoded& destination, Measurements& measurements) {
    measurements = {};
    seek(source, 0);
    std::array<std::uint8_t, 8> identifier{};
    read(source, identifier);
    require(identifier == signature, "collection signature");
    Bytes header = read_chunk(source, {83, 72, 68, 82}, 24);
    require(header.size() == 24 && get(header, 0, 4) == 0 && get(header, 4, 4) <= 1, "collection header");
    std::size_t count = static_cast<std::size_t>(get(header, 8, 4));
    require(count > 0 && count <= maximum_images, "collection count");
    destination = {};
    Layout& layout = destination.layout;
    layout.form = static_cast<Form>(get(header, 4, 4));
    layout.canvas = {static_cast<std::size_t>(get(header, 16, 4)),
        static_cast<std::size_t>(get(header, 12, 4)), static_cast<std::size_t>(get(header, 20, 4))};
    if (layout.form == Form::composite) { validate_dimensions(layout.canvas); }
    else { require(layout.canvas.width == 0 && layout.canvas.height == 0 && layout.canvas.channels == 0, "packed canvas"); }
    Bytes packed = read_chunk(source, {68, 73, 82, 69}, count * 40);
    require(packed.size() == count * 40, "collection directory");
    std::streampos body_begin = source.tellg();
    source.seekg(0, std::ios::end);
    std::streampos file_end = source.tellg();
    require(body_begin >= 0 && file_end >= body_begin, "collection file extent");
    std::uint64_t next = static_cast<std::uint64_t>(body_begin);
    std::uint64_t file_size = static_cast<std::uint64_t>(file_end);
    layout.entries.resize(count);
    std::size_t covered_rows = 0;
    for (std::size_t index = 0; index < count; ++index) {
        Entry& entry = layout.entries[index];
        std::size_t offset = index * 40;
        entry.id.value = static_cast<std::uint32_t>(get(packed, offset, 4));
        entry.dimensions = {static_cast<std::size_t>(get(packed, offset + 8, 4)),
            static_cast<std::size_t>(get(packed, offset + 4, 4)), static_cast<std::size_t>(get(packed, offset + 12, 4))};
        validate_dimensions(entry.dimensions);
        entry.x = static_cast<std::size_t>(get(packed, offset + 16, 4));
        entry.y = static_cast<std::size_t>(get(packed, offset + 20, 4));
        entry.offset.bytes = get(packed, offset + 24, 8);
        entry.length = get(packed, offset + 32, 8);
        for (std::size_t prior = 0; prior < index; ++prior) {
            require(layout.entries[prior].id.value != entry.id.value, "duplicate collection id");
        }
        if (layout.form == Form::packed) {
            require(entry.x == 0 && entry.y == 0, "packed slice origin");
        } else {
            require(entry.x == 0 && entry.y == covered_rows && covered_rows <= layout.canvas.height
                && entry.dimensions.height <= layout.canvas.height - covered_rows
                && entry.dimensions.width <= layout.canvas.width
                && entry.dimensions.channels <= layout.canvas.channels, "composite slice extent");
            covered_rows += entry.dimensions.height;
            if (index != 0) {
                require(entry.offset.bytes == layout.entries[0].offset.bytes
                    && entry.length == layout.entries[0].length, "single composite body");
                continue;
            }
        }
        require(entry.offset.bytes == next && next <= file_size && entry.length >= 12
            && entry.length <= file_size - next && entry.length - 12 <= std::numeric_limits<std::uint32_t>::max(), "collection image extent");
        next += entry.length;
    }
    if (layout.form == Form::composite) { require(covered_rows == layout.canvas.height, "composite row coverage"); }
    require(next <= file_size && file_size - next == 12, "collection ending extent");
    seek(source, next);
    Bytes ending = read_chunk(source, {83, 69, 78, 68}, 0);
    require(ending.empty(), "collection end");
    std::size_t plane_count = layout.form == Form::packed ? count : 1;
    destination.planes.resize(plane_count);
    for (std::size_t index = 0; index < plane_count; ++index) {
        Dimensions dimensions = layout.form == Form::packed ? layout.entries[index].dimensions : layout.canvas;
        decode_image(source, layout.entries[index], dimensions, destination.planes[index], measurements);
    }
    measurements.workspace_bytes += packed.capacity() + layout.entries.capacity() * sizeof(Entry);
    measurements.workspace_bytes += destination.planes.capacity() * sizeof(Bytes);
    for (const Bytes& pixels : destination.planes) { measurements.workspace_bytes += pixels.capacity(); }
}
void write_image(const Decoded& source, ImageId id, std::ostream& destination) {
    std::size_t selected = source.layout.entries.size();
    for (std::size_t index = 0; index < source.layout.entries.size(); ++index) {
        if (source.layout.entries[index].id.value == id.value) { selected = index; break; }
    }
    require(selected != source.layout.entries.size(), "collection image id");
    const Entry& entry = source.layout.entries[selected];
    if (source.layout.form == Form::packed) {
        require(source.planes.size() == source.layout.entries.size(), "packed plane count");
        write(destination, source.planes[selected]);
        return;
    }
    require(source.planes.size() == 1, "composite plane count");
    std::size_t stride = source.layout.canvas.width * source.layout.canvas.channels;
    const Bytes& canvas = source.planes[0];
    if (entry.dimensions.channels == source.layout.canvas.channels) {
        for (std::size_t y = 0; y < entry.dimensions.height; ++y) {
            std::span<const std::uint8_t> row(canvas.data() + (entry.y + y) * stride, entry.dimensions.width * entry.dimensions.channels);
            write(destination, row);
        }
    } else {
        Bytes row(entry.dimensions.width * 3);
        for (std::size_t y = 0; y < entry.dimensions.height; ++y) {
            const std::uint8_t* pixels = canvas.data() + (entry.y + y) * stride;
            for (std::size_t x = 0; x < entry.dimensions.width; ++x) {
                row[x * 3] = pixels[x * 4];
                row[x * 3 + 1] = pixels[x * 4 + 1];
                row[x * 3 + 2] = pixels[x * 4 + 2];
            }
            write(destination, row);
        }
    }
}
}
