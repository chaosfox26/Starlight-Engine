#include "graphics_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace starlight::gfxrt {
namespace {

struct Bits {
    const unsigned char* p;
    std::size_t n, pos = 0;
    std::uint32_t buf = 0;
    int cnt = 0;
    bool bad = false;
    int get(int need) {
        std::uint64_t val = buf;
        while (cnt < need) {
            if (pos >= n) { bad = true; return 0; }
            val |= static_cast<std::uint64_t>(p[pos++]) << cnt;
            cnt += 8;
        }
        buf = static_cast<std::uint32_t>(val >> need);
        cnt -= need;
        return static_cast<int>(val & ((1ull << need) - 1));
    }
};
struct Huff { short count[16]; short symbol[288]; };

int construct(Huff& h, const short* length, int n) {
    for (int l = 0; l <= 15; ++l) h.count[l] = 0;
    for (int s = 0; s < n; ++s) ++h.count[length[s]];
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l <= 15; ++l) { left <<= 1; left -= h.count[l]; if (left < 0) return left; }
    short offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = static_cast<short>(offs[l] + h.count[l]);
    for (int s = 0; s < n; ++s) if (length[s]) h.symbol[offs[length[s]]++] = static_cast<short>(s);
    return left;
}
int decode(Bits& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= 15; ++l) {
        code |= b.get(1);
        if (b.bad) return -1;
        int count = h.count[l];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -10;
}

const short kLens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

const char* codes(Bits& b, std::string& out, std::uint64_t limit, const Huff& lc, const Huff& dc) {
    while (true) {
        int s = decode(b, lc);
        if (s < 0) return b.bad ? "compressed data ends early" : "invalid code";
        if (s < 256) {
            if (out.size() >= limit) return "expands beyond its declared size";
            out.push_back(static_cast<char>(s));
        } else if (s == 256) {
            return nullptr;
        } else {
            s -= 257;
            if (s >= 29) return "invalid length code";
            int len = kLens[s] + b.get(kLext[s]);
            int ds = decode(b, dc);
            if (ds < 0 || ds >= 30) return b.bad ? "compressed data ends early" : "invalid distance code";
            std::size_t dist = static_cast<std::size_t>(kDists[ds]) + b.get(kDext[ds]);
            if (b.bad) return "compressed data ends early";
            if (dist > out.size()) return "distance too far back";
            if (out.size() + len > limit) return "expands beyond its declared size";
            for (int i = 0; i < len; ++i) out.push_back(out[out.size() - dist]);
        }
    }
}

const char* stored(Bits& b, std::string& out, std::uint64_t limit) {
    b.buf = 0;
    b.cnt = 0;
    if (b.pos + 4 > b.n) return "stored block is truncated";
    unsigned len = b.p[b.pos] | (b.p[b.pos + 1] << 8), nlen = b.p[b.pos + 2] | (b.p[b.pos + 3] << 8);
    b.pos += 4;
    if (len != (~nlen & 0xFFFF)) return "stored block length check failed";
    if (b.pos + len > b.n) return "stored block is truncated";
    if (out.size() + len > limit) return "expands beyond its declared size";
    out.append(reinterpret_cast<const char*>(b.p + b.pos), len);
    b.pos += len;
    return nullptr;
}

const char* fixed(Bits& b, std::string& out, std::uint64_t limit) {
    static Huff lc, dc;
    static bool built = false;
    if (!built) {
        short l[288];
        int s = 0;
        for (; s < 144; ++s) l[s] = 8;
        for (; s < 256; ++s) l[s] = 9;
        for (; s < 280; ++s) l[s] = 7;
        for (; s < 288; ++s) l[s] = 8;
        construct(lc, l, 288);
        for (s = 0; s < 30; ++s) l[s] = 5;
        construct(dc, l, 30);
        built = true;
    }
    return codes(b, out, limit, lc, dc);
}

const char* dynamic(Bits& b, std::string& out, std::uint64_t limit) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    int nlen = b.get(5) + 257, ndist = b.get(5) + 1, ncode = b.get(4) + 4;
    if (nlen > 286 || ndist > 30) return "too many length or distance codes";
    int index = 0;
    for (; index < ncode; ++index) lengths[order[index]] = static_cast<short>(b.get(3));
    for (; index < 19; ++index) lengths[order[index]] = 0;
    Huff lc, dc;
    if (construct(lc, lengths, 19) != 0) return "bad code-length code";
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode(b, lc);
        if (sym < 0) return "invalid code-length symbol";
        if (sym < 16) {
            lengths[index++] = static_cast<short>(sym);
        } else {
            int len = 0;
            if (sym == 16) {
                if (index == 0) return "repeat with no previous length";
                len = lengths[index - 1];
                sym = 3 + b.get(2);
            } else if (sym == 17) {
                sym = 3 + b.get(3);
            } else {
                sym = 11 + b.get(7);
            }
            if (index + sym > nlen + ndist) return "too many lengths";
            while (sym--) lengths[index++] = static_cast<short>(len);
        }
        if (b.bad) return "compressed data ends early";
    }
    if (lengths[256] == 0) return "no end-of-block code";
    int err = construct(lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return "bad literal/length code";
    err = construct(dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return "bad distance code";
    return codes(b, out, limit, lc, dc);
}

std::uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
unsigned le16(const unsigned char* p) { return p[0] | (p[1] << 8); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool valid_utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int extra = c < 0x80 ? 0 : (c >= 0xC2 && c <= 0xDF) ? 1 : (c >= 0xE0 && c <= 0xEF) ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
        if (extra < 0 || i + extra >= s.size()) return false;
        std::uint32_t cp = extra == 0 ? c : c & (0x3F >> extra);
        for (int k = 1; k <= extra; ++k) {
            unsigned char t = static_cast<unsigned char>(s[i + k]);
            if ((t & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (t & 0x3F);
        }
        if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += extra + 1;
    }
    return true;
}

}

bool inflate_raw(std::string_view in, std::string& out, std::uint64_t expected, std::string& error) {
    Bits b{reinterpret_cast<const unsigned char*>(in.data()), in.size()};
    out.clear();
    out.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(expected, 1u << 26)));
    int last;
    do {
        last = b.get(1);
        int type = b.get(2);
        if (b.bad) { error = "compressed data ends early"; return false; }
        const char* e = type == 0 ? stored(b, out, expected) : type == 1 ? fixed(b, out, expected) : type == 2 ? dynamic(b, out, expected) : "invalid block type";
        if (e) { error = e; return false; }
    } while (!last);
    if (out.size() != expected) { error = "size differs from its declared size"; return false; }
    return true;
}

Result extract_zip_bytes(std::string_view zip, const fs::path& dest, const ZipLimits& lim) {
    std::error_code ec;
    if (fs::exists(dest, ec)) return Result::fail("Extraction target already exists");
    auto p = reinterpret_cast<const unsigned char*>(zip.data());
    auto fail = [&](const std::string& m) { return Result::fail("Archive rejected: " + m); };
    if (zip.size() < 22) return fail("too small to be a ZIP file");
    std::size_t eocd = std::string::npos;
    for (std::size_t i = zip.size() - 22, stop = zip.size() > 65557 ? zip.size() - 65557 : 0;; --i) {
        if (le32(p + i) == 0x06054b50u) { eocd = i; break; }
        if (i == stop || i == 0) break;
    }
    if (eocd == std::string::npos) return fail("no end-of-archive record");
    unsigned disk = le16(p + eocd + 4), cdDisk = le16(p + eocd + 6), here = le16(p + eocd + 8), total = le16(p + eocd + 10);
    std::uint32_t cdSize = le32(p + eocd + 12), cdOff = le32(p + eocd + 16);
    if (disk || cdDisk || here != total) return fail("multi-part archives are not supported");
    if (total == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) return fail("ZIP64 archives are not supported");
    if (total > lim.maxEntries) return fail("too many entries");
    if (static_cast<std::uint64_t>(cdOff) + cdSize > eocd) return fail("central directory is out of range");

    struct Entry { std::string name; unsigned method, flags; std::uint32_t crc, csize, usize, off; bool dir; };
    std::vector<Entry> entries;
    std::set<std::string> seen, fileNames;
    std::uint64_t sum = 0;
    std::size_t at = cdOff;
    for (unsigned i = 0; i < total; ++i) {
        if (at + 46 > cdOff + cdSize || le32(p + at) != 0x02014b50u) return fail("central directory is damaged");
        Entry e{};
        unsigned madeBy = le16(p + at + 4);
        e.flags = le16(p + at + 8);
        e.method = le16(p + at + 10);
        e.crc = le32(p + at + 16);
        e.csize = le32(p + at + 20);
        e.usize = le32(p + at + 24);
        unsigned nlen = le16(p + at + 28), elen = le16(p + at + 30), clen = le16(p + at + 32);
        std::uint32_t ext = le32(p + at + 38);
        e.off = le32(p + at + 42);
        if (at + 46 + nlen + elen + clen > cdOff + cdSize) return fail("central directory is damaged");
        e.name.assign(reinterpret_cast<const char*>(p + at + 46), nlen);
        at += 46 + nlen + elen + clen;
        if (e.flags & 1) return fail("encrypted entries are not supported");
        if ((madeBy >> 8) == 3 && ((ext >> 16) & 0xF000u) == 0xA000u) return fail("link entries are not allowed");
        if (e.usize == 0xFFFFFFFFu || e.csize == 0xFFFFFFFFu || e.off == 0xFFFFFFFFu) return fail("ZIP64 entries are not supported");
        e.dir = !e.name.empty() && e.name.back() == '/';
        std::string path = e.dir ? e.name.substr(0, e.name.size() - 1) : e.name, why;
        if (!valid_utf8(path)) return fail("entry name is not valid UTF-8");
        if (!safe_relative_path(path, why)) return fail("unsafe entry name '" + path + "' (" + why + ")");
        if (!seen.insert(lower(path)).second) return fail("duplicate or case-colliding entry " + path);
        if (e.dir) {
            if (e.usize || e.csize) return fail("directory entry has data");
        } else {
            if (e.method != 0 && e.method != 8) return fail("unsupported compression method for " + path);
            if (e.usize > lim.maxFile) return fail("entry too large: " + path);
            sum += e.usize;
            if (sum > lim.maxTotal) return fail("total expanded size too large");
            if (e.method == 0 && e.csize != e.usize) return fail("stored entry sizes differ: " + path);
            if (e.method == 8 && e.usize > lim.ratioFloor && (e.csize == 0 || e.usize / e.csize > lim.maxRatio)) return fail("suspicious expansion ratio: " + path);
            fileNames.insert(lower(path));
        }
        entries.push_back(std::move(e));
    }
    for (auto& e : entries) {
        std::string path = lower(e.dir ? e.name.substr(0, e.name.size() - 1) : e.name);
        for (auto s = path.find('/'); s != std::string::npos; s = path.find('/', s + 1))
            if (fileNames.count(path.substr(0, s))) return fail("entry " + e.name + " lies inside a file");
    }

    fs::create_directories(dest.parent_path(), ec);
    if (!fs::create_directory(dest, ec)) return Result::fail("Cannot create extraction folder");
    auto abort = [&](const std::string& m) { std::error_code e2; fs::remove_all(dest, e2); return fail(m); };
    std::uint64_t written = 0;
    for (auto& e : entries) {
        auto target = dest / from_utf8(e.dir ? e.name.substr(0, e.name.size() - 1) : e.name);
        if (e.dir) { fs::create_directories(target, ec); continue; }
        if (static_cast<std::uint64_t>(e.off) + 30 > zip.size() || le32(p + e.off) != 0x04034b50u) return abort("local header is damaged for " + e.name);
        unsigned nlen = le16(p + e.off + 26), elen = le16(p + e.off + 28);
        std::size_t data = static_cast<std::size_t>(e.off) + 30 + nlen + elen;
        if (data + e.csize > zip.size() || data + e.csize > cdOff) return abort("data is out of range for " + e.name);
        if (std::string_view(zip.data() + e.off + 30, nlen) != e.name) return abort("local and central names differ for " + e.name);
        std::string content;
        std::string_view raw(zip.data() + data, e.csize);
        if (e.method == 0) {
            content.assign(raw);
        } else {
            std::string err;
            if (!inflate_raw(raw, content, e.usize, err)) return abort(e.name + ": " + err);
        }
        if (crc32_bytes(content) != e.crc) return abort("checksum mismatch for " + e.name);
        written += content.size();
        if (written > lim.maxTotal) return abort("total expanded size too large");
        fs::create_directories(target.parent_path(), ec);
        if (is_reparse_or_link(target.parent_path())) return abort("extraction would pass through a link");
        std::ofstream f(target, std::ios::binary | std::ios::trunc);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        f.close();
        if (!f) return abort("cannot write " + e.name);
    }
    return Result::good();
}

Result extract_zip(const fs::path& zipPath, const fs::path& dest, const ZipLimits& lim) {
    std::error_code ec;
    auto size = fs::file_size(zipPath, ec);
    if (ec) return Result::fail("Cannot read the archive");
    if (size > lim.maxTotal) return Result::fail("Archive rejected: file too large");
    try { return extract_zip_bytes(read_text(zipPath), dest, lim); }
    catch (const std::exception& e) { return Result::fail(e.what()); }
}

}
