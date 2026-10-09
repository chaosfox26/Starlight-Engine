#include "assets.hpp"
#include "engine_assets.hpp"
#include "platform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <wincodec.h>
#include <wrl/client.h>
namespace starlight::editor {
using Microsoft::WRL::ComPtr;
namespace {
constexpr char kBlobMagic[4] = {'S', 'L', 'B', 'L'};
constexpr uint32_t kBlobEnvelopeVersion = 1;
constexpr size_t kBlobFixedBytes = 24;
constexpr size_t kMaxTypeBytes = 64;
size_t align8(size_t value) { return (value + 7) & ~size_t(7); }
struct BlobTypes {
    std::mutex lock;
    std::unordered_map<std::string, BlobValidator> validators;
    BlobTypes() { validators.emplace(UvAtlasType, validate_uv_atlas_data); }
};
BlobTypes &blob_types() {
    static BlobTypes types;
    return types;
}
uint32_t type_version(const std::string &type) {
    const auto slash = type.rfind('/');
    return uint32_t(std::stoul(type.substr(slash + 2)));
}
struct ComScope {
    bool release = false;
    ComScope() {
        const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(result)) release = true;
        else if (result != RPC_E_CHANGED_MODE) throw std::runtime_error("COM could not be initialized for image decoding");
    }
    ~ComScope() {
        if (release) CoUninitialize();
    }
};
uint32_t crc32_of(std::string_view bytes) {
    static const auto table = [] {
        std::array<uint32_t, 256> made{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            made[n] = c;
        }
        return made;
    }();
    uint32_t c = 0xffffffffu;
    for (const unsigned char byte : bytes) c = table[(c ^ byte) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
uint32_t be32(std::string_view bytes, size_t at) {
    return (uint32_t(uint8_t(bytes[at])) << 24) | (uint32_t(uint8_t(bytes[at + 1])) << 16) | (uint32_t(uint8_t(bytes[at + 2])) << 8) | uint8_t(bytes[at + 3]);
}
struct RowTracker {
    std::vector<std::pair<uint64_t, uint64_t>> segments;
    size_t segment = 0;
    uint64_t rowLength = 0, rowsLeft = 0, position = 0;
    void start() {
        while (segment < segments.size() && !segments[segment].second) ++segment;
        if (segment < segments.size()) {
            rowLength = segments[segment].first;
            rowsLeft = segments[segment].second;
        }
    }
    bool done() const { return segment >= segments.size(); }
    void next(uint8_t byte) {
        if (done()) throw std::runtime_error("The PNG holds more pixel data than the image needs");
        if (!position && byte > 4) throw std::runtime_error("The PNG pixel data uses an invalid row filter");
        if (++position == rowLength) {
            position = 0;
            if (!--rowsLeft) {
                ++segment;
                start();
            }
        }
    }
};
struct Inflater {
    const uint8_t *in;
    size_t size, at = 0;
    uint32_t buffer = 0;
    int count = 0;
    std::array<uint8_t, 32768> window{};
    uint64_t total = 0, limit;
    uint32_t a = 1, b = 0;
    RowTracker rows;
    Inflater(const uint8_t *data, size_t length, uint64_t expected) : in(data), size(length), limit(expected) {}
    uint32_t bits(int need) {
        while (count < need) {
            if (at >= size) throw std::runtime_error("The PNG pixel stream ends early");
            buffer |= uint32_t(in[at++]) << count;
            count += 8;
        }
        const uint32_t value = buffer & ((1u << need) - 1);
        buffer >>= need;
        count -= need;
        return value;
    }
    void put(uint8_t byte) {
        if (total >= limit) throw std::runtime_error("The PNG holds more pixel data than the image needs");
        rows.next(byte);
        window[total & 32767] = byte;
        ++total;
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    struct Table {
        std::array<uint16_t, 16> counts{};
        std::array<uint16_t, 288> symbols{};
    };
    static void build(Table &table, const uint8_t *lengths, int n) {
        table.counts.fill(0);
        for (int i = 0; i < n; ++i) ++table.counts[lengths[i]];
        int left = 1;
        for (int length = 1; length <= 15; ++length) {
            left <<= 1;
            left -= table.counts[length];
            if (left < 0) throw std::runtime_error("The PNG pixel stream has an over-subscribed code");
        }
        std::array<uint16_t, 16> offsets{};
        for (int length = 1; length < 15; ++length) offsets[length + 1] = uint16_t(offsets[length] + table.counts[length]);
        for (int i = 0; i < n; ++i)
            if (lengths[i]) table.symbols[offsets[lengths[i]]++] = uint16_t(i);
    }
    int decode(const Table &table) {
        int code = 0, first = 0, index = 0;
        for (int length = 1; length <= 15; ++length) {
            code |= int(bits(1));
            const int amount = table.counts[length];
            if (code - amount < first) return table.symbols[index + (code - first)];
            index += amount;
            first += amount;
            first <<= 1;
            code <<= 1;
        }
        throw std::runtime_error("The PNG pixel stream has an invalid code");
    }
    void codes(const Table &literals, const Table &distances) {
        static const uint16_t lengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint16_t lengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t distanceBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint16_t distanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(literals);
            if (symbol < 256) {
                put(uint8_t(symbol));
            } else if (symbol == 256) {
                return;
            } else {
                symbol -= 257;
                if (symbol >= 29) throw std::runtime_error("The PNG pixel stream has an invalid length code");
                const uint32_t length = lengthBase[symbol] + bits(lengthExtra[symbol]);
                const int distanceSymbol = decode(distances);
                if (distanceSymbol >= 30) throw std::runtime_error("The PNG pixel stream has an invalid distance code");
                const uint32_t distance = distanceBase[distanceSymbol] + bits(distanceExtra[distanceSymbol]);
                if (distance > total || distance > 32768) throw std::runtime_error("The PNG pixel stream points before its own start");
                for (uint32_t i = 0; i < length; ++i) put(window[(total - distance) & 32767]);
            }
        }
    }
    void run() {
        bool last;
        do {
            last = bits(1);
            const uint32_t type = bits(2);
            if (type == 0) {
                buffer = 0;
                count = 0;
                if (size - at < 4) throw std::runtime_error("The PNG pixel stream ends early");
                const uint32_t length = in[at] | (uint32_t(in[at + 1]) << 8), complement = in[at + 2] | (uint32_t(in[at + 3]) << 8);
                at += 4;
                if (length != (~complement & 0xffff)) throw std::runtime_error("The PNG pixel stream has a damaged stored block");
                if (size - at < length) throw std::runtime_error("The PNG pixel stream ends early");
                for (uint32_t i = 0; i < length; ++i) put(in[at++]);
            } else if (type == 1) {
                static const Table literals = [] {
                    uint8_t lengths[288];
                    for (int i = 0; i < 144; ++i) lengths[i] = 8;
                    for (int i = 144; i < 256; ++i) lengths[i] = 9;
                    for (int i = 256; i < 280; ++i) lengths[i] = 7;
                    for (int i = 280; i < 288; ++i) lengths[i] = 8;
                    Table table;
                    build(table, lengths, 288);
                    return table;
                }();
                static const Table distances = [] {
                    uint8_t lengths[30];
                    for (auto &length : lengths) length = 5;
                    Table table;
                    build(table, lengths, 30);
                    return table;
                }();
                codes(literals, distances);
            } else if (type == 2) {
                static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                const int literalCount = int(bits(5)) + 257, distanceCount = int(bits(5)) + 1, codeCount = int(bits(4)) + 4;
                if (literalCount > 286 || distanceCount > 30) throw std::runtime_error("The PNG pixel stream has too many codes");
                uint8_t lengths[320] = {};
                for (int i = 0; i < codeCount; ++i) lengths[order[i]] = uint8_t(bits(3));
                Table lengthTable;
                build(lengthTable, lengths, 19);
                int index = 0;
                uint8_t all[320] = {};
                while (index < literalCount + distanceCount) {
                    int symbol = decode(lengthTable);
                    if (symbol < 16) {
                        all[index++] = uint8_t(symbol);
                    } else {
                        uint8_t value = 0;
                        int repeat;
                        if (symbol == 16) {
                            if (!index) throw std::runtime_error("The PNG pixel stream repeats a missing length");
                            value = all[index - 1];
                            repeat = 3 + int(bits(2));
                        } else if (symbol == 17) {
                            repeat = 3 + int(bits(3));
                        } else {
                            repeat = 11 + int(bits(7));
                        }
                        if (index + repeat > literalCount + distanceCount) throw std::runtime_error("The PNG pixel stream has too many lengths");
                        while (repeat--) all[index++] = value;
                    }
                }
                if (!all[256]) throw std::runtime_error("The PNG pixel stream has no end code");
                Table literals, distances;
                build(literals, all, literalCount);
                build(distances, all + literalCount, distanceCount);
                codes(literals, distances);
            } else
                throw std::runtime_error("The PNG pixel stream has an invalid block type");
        } while (!last);
    }
};
std::string hresult_text(HRESULT result) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(result));
    return text;
}
std::string pixel_format_name(const GUID &format) {
    static const std::pair<const GUID *, const char *> names[] = {
        {&GUID_WICPixelFormat1bppIndexed, "1bpp indexed"}, {&GUID_WICPixelFormat2bppIndexed, "2bpp indexed"},
        {&GUID_WICPixelFormat4bppIndexed, "4bpp indexed"}, {&GUID_WICPixelFormat8bppIndexed, "8bpp indexed"},
        {&GUID_WICPixelFormatBlackWhite, "1bpp black and white"}, {&GUID_WICPixelFormat2bppGray, "2bpp gray"},
        {&GUID_WICPixelFormat4bppGray, "4bpp gray"}, {&GUID_WICPixelFormat8bppGray, "8bpp gray"},
        {&GUID_WICPixelFormat16bppGray, "16bpp gray"}, {&GUID_WICPixelFormat24bppBGR, "24bpp RGB"},
        {&GUID_WICPixelFormat24bppRGB, "24bpp RGB"}, {&GUID_WICPixelFormat32bppBGR, "32bpp RGB"},
        {&GUID_WICPixelFormat32bppBGRA, "32bpp RGBA"}, {&GUID_WICPixelFormat32bppRGBA, "32bpp RGBA"},
        {&GUID_WICPixelFormat48bppRGB, "48bpp RGB"}, {&GUID_WICPixelFormat64bppRGBA, "64bpp RGBA"},
        {&GUID_WICPixelFormat64bppBGRA, "64bpp RGBA"}};
    for (const auto &entry : names)
        if (IsEqualGUID(*entry.first, format)) return entry.second;
    return "other";
}
}
uint32_t crc32_bytes(std::string_view bytes) { return crc32_of(bytes); }
void validate_jpeg_stream(std::string_view bytes) {
    if (bytes.size() < 4 || uint8_t(bytes[0]) != 0xff || uint8_t(bytes[1]) != 0xd8 || uint8_t(bytes[2]) != 0xff) throw std::runtime_error("The data is not a JPEG");
    size_t at = 2;
    bool frame = false, tables = false, huffman = false, scanned = false;
    auto byte = [&](size_t index) { return uint8_t(bytes[index]); };
    for (;;) {
        if (at + 2 > bytes.size() || byte(at) != 0xff) throw std::runtime_error("The JPEG segment layout is damaged");
        while (at + 1 < bytes.size() && byte(at + 1) == 0xff) ++at;
        if (at + 2 > bytes.size()) throw std::runtime_error("The JPEG is truncated");
        const uint8_t marker = byte(at + 1);
        if (marker == 0xd9) {
            if (!scanned) throw std::runtime_error("The JPEG ends before any image scan");
            for (size_t i = at + 2; i < bytes.size(); ++i)
                if (bytes[i]) throw std::runtime_error("The JPEG has data after its end marker");
            return;
        }
        if (marker == 0x00 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd8)) throw std::runtime_error("The JPEG has a marker where a segment must start");
        if (at + 4 > bytes.size()) throw std::runtime_error("The JPEG is truncated");
        const size_t length = (size_t(byte(at + 2)) << 8) | byte(at + 3);
        if (length < 2 || at + 2 + length > bytes.size()) throw std::runtime_error("The JPEG is truncated");
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (frame || length < 8) throw std::runtime_error("The JPEG frame header is damaged or repeated");
            const size_t components = byte(at + 9);
            if (!components || length != 8 + 3 * components) throw std::runtime_error("The JPEG frame header does not match its component count");
            frame = true;
        } else if (marker == 0xdb) {
            tables = true;
        } else if (marker == 0xc4) {
            huffman = true;
        }
        if (marker == 0xda) {
            if (!frame || !tables || !huffman) throw std::runtime_error("The JPEG scan comes before its frame or tables");
            const size_t components = byte(at + 4);
            if (!components || length != 6 + 2 * components) throw std::runtime_error("The JPEG scan header does not match its component count");
            size_t scan = at + 2 + length, entropy = 0;
            int restart = -1;
            for (;;) {
                if (scan >= bytes.size()) throw std::runtime_error("The JPEG is truncated inside its image data");
                if (byte(scan) != 0xff) {
                    ++scan;
                    ++entropy;
                    continue;
                }
                size_t next = scan + 1;
                while (next < bytes.size() && byte(next) == 0xff) ++next;
                if (next >= bytes.size()) throw std::runtime_error("The JPEG is truncated inside its image data");
                const uint8_t code = byte(next);
                if (code == 0x00) {
                    scan = next + 1;
                    entropy += 2;
                    continue;
                }
                if (code >= 0xd0 && code <= 0xd7) {
                    const int index = code - 0xd0;
                    if (restart >= 0 && index != (restart + 1) % 8) throw std::runtime_error("The JPEG restart markers are out of sequence");
                    restart = index;
                    scan = next + 1;
                    continue;
                }
                if (code == 0xd9 || code == 0xda || code == 0xc4 || code == 0xdb || code == 0xdd || (code >= 0xe0 && code <= 0xef) || code == 0xfe) {
                    at = next - 1;
                    break;
                }
                throw std::runtime_error("The JPEG image data holds an unexpected marker");
            }
            if (!entropy) throw std::runtime_error("The JPEG scan holds no image data");
            scanned = true;
            continue;
        }
        at += 2 + length;
    }
}
uint64_t validate_png_stream(std::string_view bytes) {
    static constexpr unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), signature, 8)) throw std::runtime_error("The data is not a PNG");
    size_t at = 8;
    bool header = false, palette = false, data = false, dataEnded = false, end = false, transparency = false;
    uint64_t paletteEntries = 0;
    uint32_t width = 0, height = 0;
    uint8_t depth = 0, colour = 0, interlace = 0;
    std::string stream;
    while (at < bytes.size()) {
        if (end) throw std::runtime_error("The PNG has " + std::to_string(bytes.size() - at) + " bytes after its end marker, starting at byte " + std::to_string(at) + ". The PNG specification lets a reader ignore them, but Starlight does not accept data that no decoder checks, so remove the trailing bytes");
        if (bytes.size() - at < 12) throw std::runtime_error("The PNG is truncated inside the chunk header at byte " + std::to_string(at));
        const uint64_t length = be32(bytes, at);
        if (length > 0x7fffffffull || length > bytes.size() - at - 12) throw std::runtime_error("The PNG is truncated inside the chunk " + std::string(bytes.substr(at + 4, 4)) + " at byte " + std::to_string(at) + " (it claims " + std::to_string(length) + " bytes)");
        const auto type = bytes.substr(at + 4, 4);
        for (const unsigned char c : type)
            if (!std::isalpha(c)) throw std::runtime_error("The PNG has a damaged chunk name at byte " + std::to_string(at + 4));
        if (crc32_of(bytes.substr(at + 4, size_t(length) + 4)) != be32(bytes, at + 8 + size_t(length)))
            throw std::runtime_error("The PNG chunk " + std::string(type) + " at byte " + std::to_string(at) + " has a bad checksum. A damaged chunk is refused even when it is optional (ancillary), because the bytes cannot be trusted; re-export the image");
        const auto body = bytes.substr(at + 8, size_t(length));
        if (!header) {
            if (type != "IHDR" || length != 13) throw std::runtime_error("The PNG does not start with a valid header chunk");
            width = be32(body, 0);
            height = be32(body, 4);
            depth = uint8_t(body[8]);
            colour = uint8_t(body[9]);
            interlace = uint8_t(body[12]);
            const bool depthOk = colour == 0 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)
                                 : colour == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8)
                                 : (colour == 2 || colour == 4 || colour == 6) ? (depth == 8 || depth == 16)
                                                                                : false;
            if (!depthOk) throw std::runtime_error("The PNG uses an unsupported bit depth or colour type");
            if (body[10] != 0 || body[11] != 0 || interlace > 1) throw std::runtime_error("The PNG uses an unsupported compression, filter or interlace method");
            const auto fit = texture_limits::checkDecodedRgba8(width, height);
            if (!fit.ok()) throw std::runtime_error("The PNG header (chunk IHDR at byte " + std::to_string(at) + "): " + texture_limits::explain(fit, width, height, texture_limits::DecodedChannels, texture_limits::DecodedBytesPerChannel));
            header = true;
        } else if (type == "IHDR") {
            throw std::runtime_error("The PNG has a second header chunk");
        } else if (type == "PLTE") {
            if (data) throw std::runtime_error("The PNG palette comes after its pixel data");
            if (palette) throw std::runtime_error("The PNG has a second palette");
            if (colour == 0 || colour == 4 || !length || length % 3 || length > 768 || (colour == 3 && length / 3 > (uint64_t(1) << depth))) throw std::runtime_error("The PNG palette is invalid");
            palette = true;
            paletteEntries = length / 3;
        } else if (type == "tRNS") {
            if (data || transparency) throw std::runtime_error("The PNG transparency chunk is repeated or comes after its pixel data");
            const bool sized = colour == 0 ? length == 2 : colour == 2 ? length == 6 : colour == 3 ? (palette && length >= 1 && length <= paletteEntries) : false;
            if (!sized) throw std::runtime_error("The PNG transparency chunk does not fit its colour type");
            transparency = true;
        } else if (type == "IDAT") {
            if (dataEnded) throw std::runtime_error("The PNG pixel chunks are not consecutive");
            data = true;
            stream.append(body);
        } else if (type == "IEND") {
            if (length) throw std::runtime_error("The PNG end marker has data");
            end = true;
        } else if (std::isupper(static_cast<unsigned char>(type[0]))) {
            throw std::runtime_error("The PNG has the critical chunk " + std::string(type) + " at byte " + std::to_string(at) + ", which this reader does not know; a critical chunk cannot be skipped safely");
        }
        if (type != "IDAT" && data) dataEnded = true;
        at += 12 + size_t(length);
    }
    if (!header || !data || !end) throw std::runtime_error("The PNG is missing its header, pixel data or end marker");
    if (colour == 3 && !palette) throw std::runtime_error("The PNG needs a palette and has none");
    const uint64_t channels = colour == 0 || colour == 3 ? 1 : colour == 2 ? 3 : colour == 4 ? 2 : 4, pixelBits = channels * depth;
    {
        const auto raw = texture_limits::checkImage(width, height, channels, depth == 16 ? 2 : 1, UINT64_MAX);
        if (!raw.ok()) throw std::runtime_error("The PNG pixel layout: " + texture_limits::explain(raw, width, height, channels, depth == 16 ? 2 : 1, UINT64_MAX));
    }
    auto rowBytes = [&](uint64_t pixels) { return 1 + (pixels * pixelBits + 7) / 8; };
    RowTracker tracker;
    uint64_t expected = 0;
    if (!interlace) {
        tracker.segments.push_back({rowBytes(width), height});
    } else {
        static const uint32_t passes[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
        for (const auto &pass : passes) {
            const uint64_t pw = width > pass[0] ? (width - pass[0] + pass[2] - 1) / pass[2] : 0, ph = height > pass[1] ? (height - pass[1] + pass[3] - 1) / pass[3] : 0;
            if (pw && ph) tracker.segments.push_back({rowBytes(pw), ph});
        }
    }
    for (const auto &segment : tracker.segments) expected += segment.first * segment.second;
    if (expected > MaxPngPixelStreamBytes) throw std::runtime_error("The PNG declares " + std::to_string(expected) + " bytes of pixel data, above the " + std::to_string(MaxPngPixelStreamBytes) + " byte (1 GiB) limit for one image. Starlight never downsamples silently, so reduce the image or split it into tiles");
    if (expected > uint64_t(stream.size()) * 1032 + 1024) throw std::runtime_error("The PNG declares more pixel data than its compressed data can hold");
    tracker.start();
    if (stream.size() < 6) throw std::runtime_error("The PNG pixel stream is too short");
    const uint8_t cmf = uint8_t(stream[0]), flg = uint8_t(stream[1]);
    if ((cmf & 15) != 8 || (cmf >> 4) > 7 || ((uint32_t(cmf) << 8) | flg) % 31 || (flg & 0x20)) throw std::runtime_error("The PNG pixel stream has an invalid header");
    Inflater inflater(reinterpret_cast<const uint8_t *>(stream.data()) + 2, stream.size() - 2, expected);
    inflater.rows = std::move(tracker);
    inflater.run();
    if (inflater.total != expected) throw std::runtime_error("The PNG holds less pixel data than the image needs");
    if (inflater.size - inflater.at != 4) throw std::runtime_error("The PNG pixel stream has unexpected trailing bytes");
    const uint32_t adler = (inflater.b << 16) | inflater.a;
    if (be32(std::string_view(stream), stream.size() - 4) != adler) throw std::runtime_error("The PNG pixel stream fails its checksum");
    return expected;
}
std::shared_ptr<const std::string> load_model_bytes(const std::string &id) { return EngineAssets::get(AssetKind::Model, id); }
bool texture_decoder_available() {
    try {
        ComScope com;
        ComPtr<IWICImagingFactory> factory;
        return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    } catch (const std::exception &) {
        return false;
    }
}
TextureDecoded decode_texture_bytes(std::string_view bytes) {
    const auto header = describe_texture(bytes);
    const auto fit = texture_limits::checkDecodedRgba8(header.width, header.height);
    if (!fit.ok()) throw std::runtime_error("The image cannot be loaded by the runtime: " + texture_limits::explain(fit, header.width, header.height, texture_limits::DecodedChannels, texture_limits::DecodedBytesPerChannel));
    const uint64_t pixelStream = header.png ? validate_png_stream(bytes) : 0;
    if (!header.png) validate_jpeg_stream(bytes);
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        throw std::runtime_error("The Windows Imaging Component is not available, so image data cannot be decoded");
    ComPtr<IWICStream> stream;
    auto result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromMemory(reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data())), DWORD(bytes.size()));
    if (FAILED(result)) throw std::runtime_error("The image could not be opened for decoding (" + hresult_text(result) + ")");
    ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(result)) throw std::runtime_error("The image data cannot be decoded (" + hresult_text(result) + ")");
    TextureDecoded decoded;
    decoded.pixelStreamBytes = pixelStream;
    GUID container{};
    if (SUCCEEDED(decoder->GetContainerFormat(&container))) decoded.container = IsEqualGUID(container, GUID_ContainerFormatPng) ? "png" : IsEqualGUID(container, GUID_ContainerFormatJpeg) ? "jpeg" : "other";
    if (decoded.container != (header.png ? "png" : "jpeg")) throw std::runtime_error("The decoder reads this image as a different format than its header declares");
    UINT frames = 0;
    if (FAILED(decoder->GetFrameCount(&frames)) || frames < 1) throw std::runtime_error("The image has no frame to decode");
    decoded.frames = frames;
    ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result)) throw std::runtime_error("The first image frame cannot be read (" + hresult_text(result) + ")");
    UINT width = 0, height = 0;
    result = frame->GetSize(&width, &height);
    if (FAILED(result)) throw std::runtime_error("The image size cannot be read (" + hresult_text(result) + ")");
    if (width != header.width || height != header.height) throw std::runtime_error("The decoded image size differs from the size in its header");
    WICPixelFormatGUID format{};
    if (SUCCEEDED(frame->GetPixelFormat(&format))) decoded.pixelFormat = pixel_format_name(format);
    if (header.png) {
        const bool declared16 = uint8_t(bytes[24]) == 16;
        const bool decoded16 = decoded.pixelFormat == "16bpp gray" || decoded.pixelFormat == "48bpp RGB" || decoded.pixelFormat == "64bpp RGBA";
        if (declared16 != decoded16) throw std::runtime_error("The decoder reports " + decoded.pixelFormat + " but the PNG header declares " + std::to_string(int(uint8_t(bytes[24]))) + " bits per channel");
    }
    ComPtr<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(result)) throw std::runtime_error("The image pixel format cannot be converted for use (" + hresult_text(result) + ")");
    const uint64_t stride = uint64_t(width) * 4;
    if (stride > 0x7fffffffull) throw std::runtime_error("The image is too wide to decode");
    const UINT band = UINT(std::clamp<uint64_t>((8ull << 20) / stride, 1, height));
    std::vector<BYTE> pixels(size_t(stride) * band);
    for (UINT y = 0; y < height; y += band) {
        const UINT rows = std::min(band, height - y);
        WICRect area{0, INT(y), INT(width), INT(rows)};
        result = converter->CopyPixels(&area, UINT(stride), UINT(stride * rows), pixels.data());
        if (FAILED(result)) throw std::runtime_error("The image pixel data is damaged: decoding failed at row " + std::to_string(y) + " (" + hresult_text(result) + ")");
        decoded.decodedBytes += stride * rows;
    }
    decoded.width = width;
    decoded.height = height;
    return decoded;
}
TextureDecoded decode_texture(const std::string &id) { return decode_texture_bytes(*load_texture(id)); }
void validate_blob_type_name(const std::string &type) {
    if (type.empty() || type.size() > kMaxTypeBytes) throw std::runtime_error("A blob type name must be 1 to 64 characters");
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t at = 0; at <= type.size(); ++at)
        if (at == type.size() || type[at] == '/') {
            parts.push_back(type.substr(start, at - start));
            start = at + 1;
        }
    if (parts.size() < 3) throw std::runtime_error("A blob type must look like family/encoding/vN");
    for (const auto &part : parts) {
        if (part.empty()) throw std::runtime_error("A blob type has an empty part");
        for (const unsigned char c : part)
            if (!(std::islower(c) || std::isdigit(c) || c == '-' || c == '.')) throw std::runtime_error("A blob type may use only lowercase letters, digits, '-', '.' and '/'");
    }
    if (!std::islower(static_cast<unsigned char>(parts.front().front()))) throw std::runtime_error("A blob type must start with a letter");
    const auto &version = parts.back();
    if (version.size() < 2 || version.size() > 7 || version[0] != 'v' || version[1] == '0' ||
        !std::all_of(version.begin() + 1, version.end(), [](unsigned char c) { return std::isdigit(c); }))
        throw std::runtime_error("A blob type must end with an explicit version such as v1");
}
void register_blob_type(const std::string &type, BlobValidator validator) {
    validate_blob_type_name(type);
    if (!validator) throw std::runtime_error("A blob type needs a validator");
    auto &types = blob_types();
    std::lock_guard guard(types.lock);
    types.validators[type] = std::move(validator);
}
bool blob_type_known(const std::string &type) {
    auto &types = blob_types();
    std::lock_guard guard(types.lock);
    return types.validators.contains(type);
}
void validate_uv_atlas_data(std::string_view data) {
    if (data.empty() || data.size() % 8) throw std::runtime_error("A UV atlas is a whole number of (u, v) float32 pairs, so its size must be a non-zero multiple of 8 bytes");
    const size_t count = data.size() / 8;
    for (size_t i = 0; i < count * 2; ++i) {
        float value;
        std::memcpy(&value, data.data() + i * 4, 4);
        if (!std::isfinite(value)) throw std::runtime_error("The UV atlas holds a value that is not a finite number (pair " + std::to_string(i / 2) + ")");
        if (value < 0.f || value > 1.f) throw std::runtime_error("The UV atlas holds a coordinate outside 0..1 (pair " + std::to_string(i / 2) + ")");
    }
}
BlobData parse_blob(std::shared_ptr<const std::string> envelope) {
    if (!envelope) throw std::runtime_error("The blob is empty");
    const std::string_view bytes = *envelope;
    if (bytes.size() < kBlobFixedBytes || std::memcmp(bytes.data(), kBlobMagic, 4)) throw std::runtime_error("The data is not a Starlight blob");
    uint32_t version, typeBytes, offset;
    uint64_t dataBytes;
    std::memcpy(&version, bytes.data() + 4, 4);
    std::memcpy(&typeBytes, bytes.data() + 8, 4);
    std::memcpy(&offset, bytes.data() + 12, 4);
    std::memcpy(&dataBytes, bytes.data() + 16, 8);
    if (version != kBlobEnvelopeVersion) throw std::runtime_error("The blob envelope is version " + std::to_string(version) + " but this build supports version " + std::to_string(kBlobEnvelopeVersion) + " only");
    if (!typeBytes || typeBytes > kMaxTypeBytes || kBlobFixedBytes + typeBytes > bytes.size()) throw std::runtime_error("The blob type length is invalid");
    if (offset != align8(kBlobFixedBytes + typeBytes)) throw std::runtime_error("The blob data offset is invalid");
    if (dataBytes > MaxBlobBytes) throw std::runtime_error("The blob exceeds the 64 MiB limit");
    if (offset > bytes.size() || dataBytes != bytes.size() - offset) throw std::runtime_error("The blob size does not match its envelope");
    for (size_t i = kBlobFixedBytes + typeBytes; i < offset; ++i)
        if (bytes[i]) throw std::runtime_error("The blob envelope padding is not zero");
    BlobData blob;
    blob.type.assign(bytes.substr(kBlobFixedBytes, typeBytes));
    validate_blob_type_name(blob.type);
    blob.version = type_version(blob.type);
    BlobValidator validator;
    {
        auto &types = blob_types();
        std::lock_guard guard(types.lock);
        if (auto found = types.validators.find(blob.type); found != types.validators.end()) validator = found->second;
    }
    if (!validator) throw std::runtime_error("The blob type \"" + blob.type + "\" is not registered, so its data cannot be validated");
    blob.data = bytes.substr(offset);
    validator(blob.data);
    blob.envelope = std::move(envelope);
    return blob;
}
std::string import_blob_bytes(std::string_view type, std::string_view data) {
    const std::string name(type);
    validate_blob_type_name(name);
    if (data.size() > MaxBlobBytes) throw std::runtime_error("The blob exceeds the 64 MiB limit");
    const size_t offset = align8(kBlobFixedBytes + name.size());
    std::string envelope;
    envelope.reserve(offset + data.size());
    envelope.append(kBlobMagic, 4);
    const uint32_t version = kBlobEnvelopeVersion, typeBytes = uint32_t(name.size()), dataOffset = uint32_t(offset);
    const uint64_t dataBytes = data.size();
    envelope.append(reinterpret_cast<const char *>(&version), 4);
    envelope.append(reinterpret_cast<const char *>(&typeBytes), 4);
    envelope.append(reinterpret_cast<const char *>(&dataOffset), 4);
    envelope.append(reinterpret_cast<const char *>(&dataBytes), 8);
    envelope.append(name);
    envelope.append(offset - envelope.size(), '\0');
    envelope.append(data);
    (void)parse_blob(std::make_shared<const std::string>(envelope));
    return EngineAssets::put(AssetKind::Blob, envelope);
}
std::string import_blob(const fs::path &path, std::string_view type) {
    try {
        std::error_code error;
        const auto size = fs::file_size(path, error);
        if (error) throw std::runtime_error("The file cannot be read (" + error.message() + ")");
        if (size > MaxBlobBytes) throw std::runtime_error("The blob exceeds the 64 MiB limit");
        return import_blob_bytes(type, read_file(path));
    } catch (const std::exception &e) {
        throw std::runtime_error("Cannot import the blob \"" + utf8(path.filename().wstring()) + "\": " + e.what());
    }
}
BlobData load_blob(const std::string &id) {
    auto envelope = EngineAssets::get(AssetKind::Blob, id);
    try {
        return parse_blob(std::move(envelope));
    } catch (const std::exception &e) {
        throw AssetMissing("Engine asset " + id + " is not a usable blob: " + e.what());
    }
}
size_t model_uv_vertex_count(std::string_view glb) {
    if (glb.size() < 20) throw std::runtime_error("Truncated GLB data");
    json description;
    bool found = false;
    for (size_t offset = 12; offset + 8 <= glb.size();) {
        uint32_t length, kind;
        std::memcpy(&length, glb.data() + offset, 4);
        std::memcpy(&kind, glb.data() + offset + 4, 4);
        offset += 8;
        if (length > glb.size() - offset) throw std::runtime_error("Invalid GLB chunk range");
        if (kind == 0x4e4f534a) {
            description = json::parse(glb.substr(offset, length));
            found = true;
            break;
        }
        offset += length;
    }
    if (!found) throw std::runtime_error("The GLB has no description chunk");
    size_t total = 0;
    std::unordered_set<size_t> visited;
    auto count_of = [&](const json &primitive) -> size_t {
        const auto &attributes = primitive.at("attributes");
        if (!attributes.contains("POSITION") || !attributes.contains("TEXCOORD_0")) return 0;
        if (primitive.contains("mode") && (native_int(primitive.at("mode")) < 4 || native_int(primitive.at("mode")) > 6)) return 0;
        return size_t(native_int(description.at("accessors").at(size_t(native_int(attributes.at("POSITION")))).at("count")));
    };
    std::function<void(size_t, int)> walk = [&](size_t index, int depth) {
        if (depth > 256 || !visited.insert(index).second) throw std::runtime_error("Repeated, cyclic or too-deep model node");
        const auto &node = description.at("nodes").at(index);
        if (node.contains("mesh"))
            for (const auto &primitive : description.at("meshes").at(size_t(native_int(node.at("mesh")))).at("primitives")) total += count_of(primitive);
        if (node.contains("children"))
            for (const auto &child : node.at("children")) walk(size_t(native_int(child)), depth + 1);
    };
    const size_t scene = description.contains("scene") ? size_t(native_int(description.at("scene"))) : 0;
    for (const auto &node : description.at("scenes").at(scene).at("nodes")) walk(size_t(native_int(node)), 0);
    return total;
}
size_t validate_uv_atlas_for_model(const std::string &blobId, const std::string &modelId) {
    const auto blob = load_blob(blobId);
    if (blob.type != UvAtlasType) throw std::runtime_error("Asset " + blobId + " is a \"" + blob.type + "\" blob, not a UV atlas");
    const auto model = load_model(modelId);
    const auto glb = load_model_bytes(modelId);
    const size_t atlas = blob.data.size() / 8, expected = model_uv_vertex_count(*glb);
    size_t parsed = 0;
    for (const auto &part : *model) parsed += part.mesh.vertices.size();
    if (!expected) throw std::runtime_error("Model " + modelId + " has no triangle vertices with texture coordinates, so no UV atlas can belong to it");
    if (parsed < expected) throw std::runtime_error("Model " + modelId + " parsed to fewer vertices than its UV-bearing primitives declare");
    if (atlas != expected)
        throw std::runtime_error("UV atlas " + blobId + " has " + std::to_string(atlas) + " vertices but model " + modelId + " has " + std::to_string(expected) + " (parsed model: " + std::to_string(parsed) + ")");
    return atlas;
}
AssetReadiness check_asset_ready(const std::string &id) {
    AssetReadiness result;
    try {
        if (id.starts_with("model-")) {
            const auto parts = load_model(id);
            size_t vertices = 0;
            for (const auto &part : *parts) vertices += part.mesh.vertices.size();
            result.detail = "model parsed: " + std::to_string(parts->size()) + " parts, " + std::to_string(vertices) + " vertices";
        } else if (id.starts_with("texture-")) {
            const auto decoded = decode_texture(id);
            result.detail = "texture decoded: " + std::to_string(decoded.width) + "x" + std::to_string(decoded.height) + " " + decoded.container + ", source " + decoded.pixelFormat +
                        (decoded.pixelStreamBytes ? ", pixel stream verified (" + std::to_string(decoded.pixelStreamBytes) + " bytes inflated, checksums ok)" : "");
        } else if (id.starts_with("blob-")) {
            const auto blob = load_blob(id);
            result.detail = "blob valid: " + blob.type + ", " + std::to_string(blob.data.size()) + " bytes";
        } else if (id.starts_with("sculpt-")) {
            const auto sculpt = load_sculpt(id);
            result.detail = "sculpt loaded: " + std::to_string(sculpt.Indices.size() / 3) + " triangles";
        } else if (id.starts_with("terrain-")) {
            TerrainData terrain;
            load_terrain_geometry(id, terrain);
            terrain.validate();
            result.detail = "terrain loaded";
        } else
            throw AssetMissing("Unknown engine asset id: " + id);
        EngineAssets::verify_on_disk(id);
        result.ready = true;
    } catch (const std::exception &e) {
        result.ready = false;
        result.detail = e.what();
    }
    return result;
}
}
