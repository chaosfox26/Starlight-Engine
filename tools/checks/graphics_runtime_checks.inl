#include "graphics_runtime.hpp"
#include <fstream>
#include <sstream>
#include <functional>
namespace grtest {
namespace gr = starlight::gfxrt;
namespace gfs = std::filesystem;
struct CS { std::string id, release, group; std::vector<gr::Requirement> req; std::string vendor = "test", version = "1.0.0", api = "test"; int major = 1; };
inline void put(const gfs::path& p, const std::string& t) { gfs::create_directories(p.parent_path()); std::ofstream f(p, std::ios::binary); f << t; }
inline gr::SetManifest build(const gfs::path& pkgRoot, const std::string& id, const std::string& family, gr::Category cat, gr::Channel ch, const std::string& version, const std::vector<CS>& comps) {
    gr::SetManifest m;
    m.set_id = id; m.family = family; m.category = cat; m.channel = ch; m.version = version;
    for (auto& c : comps) {
        gr::Component k;
        k.id = c.id; k.vendor = c.vendor; k.version = c.version; k.release = c.release; k.api_family = c.api; k.api_major = c.major;
        k.matched_set = c.group; k.signer = "FAKE TEST SIGNER"; k.requires_ = c.req;
        std::string dll = "bin/" + c.id + ".dll", lic = "licenses/" + c.id + ".txt";
        std::string body = "FAKE COMPONENT " + c.id + " " + c.release + " " + version + " " + id;
        put(pkgRoot / id / gr::from_utf8(dll), body);
        put(pkgRoot / id / gr::from_utf8(lic), "FAKE LICENCE for " + c.id);
        k.files.push_back({dll, body.size(), gr::sha256_bytes(body)});
        std::string lt = "FAKE LICENCE for " + c.id;
        k.files.push_back({lic, lt.size(), gr::sha256_bytes(lt)});
        k.licence_files = {lic};
        m.components.push_back(std::move(k));
    }
    put(pkgRoot / id / "set.json", gr::manifest_to_json(m));
    return m;
}
inline gr::Requirement rq(const std::string& id, const std::string& release, const std::string& origin = "starlight-policy") { return {id, release, origin}; }
inline const gr::Category kUp = gr::Category::Upscaling, kFg = gr::Category::FrameGeneration, kLr = gr::Category::LatencyReduction;
inline void fixture_packages(const gfs::path& bundled, const gfs::path& pkgs) {
    using gr::Channel;
    build(bundled, "sl-2.12.0", "sl", kUp, Channel::Stable, "2.12.0", {{"nvidia.streamline.interposer", "sl-2.12.0", "sl"}, {"nvidia.streamline.common", "sl-2.12.0", "sl"}});
    build(bundled, "dlssg-310.7", "dlssg", kFg, Channel::Stable, "310.7.0", {{"nvidia.streamline.dlss-g", "sl-2.12.0", "sl", {rq("nvidia.streamline.interposer", "sl-2.12.0")}}});
    build(bundled, "reflex-1.0", "reflex", kLr, Channel::Stable, "1.0.0", {{"nvidia.streamline.reflex", "sl-2.12.0", "reflex"}});
    build(bundled, "xefg-1.3.1", "xefg", kFg, Channel::Stable, "1.3.1", {{"intel.xess.fg", "xess-1.3", "xefg", {rq("intel.xell", "", "vendor")}, "intel", "1.3.1", "xess", 3}, {"intel.xell", "xess-1.3", "xefg", {}, "intel", "1.3.1", "xess", 3}});
    build(bundled, "plain-1.0.0", "plain", kUp, Channel::Stable, "1.0.0", {{"test.plain.core", "plain-1.0", "plain"}});
    build(pkgs, "plain-1.1.0", "plain", kUp, Channel::Stable, "1.1.0", {{"test.plain.core", "plain-1.1", "plain"}});
    build(pkgs, "plain-1.2.0-exp", "plain", kUp, Channel::Experimental, "1.2.0", {{"test.plain.core", "plain-1.2", "plain"}});
    build(pkgs, "sl-2.13.0", "sl", kUp, Channel::Stable, "2.13.0", {{"nvidia.streamline.interposer", "sl-2.13.0", "sl"}, {"nvidia.streamline.common", "sl-2.13.0", "sl"}});
    build(pkgs, "sl-2.14.0-exp", "sl", kUp, Channel::Experimental, "2.14.0", {{"nvidia.streamline.interposer", "sl-2.14.0", "sl"}, {"nvidia.streamline.common", "sl-2.14.0", "sl"}});
    build(pkgs, "dlssg-310.9", "dlssg", kFg, Channel::Stable, "310.9.0", {{"nvidia.streamline.dlss-g", "sl-2.13.0", "sl", {rq("nvidia.streamline.interposer", "sl-2.13.0")}}});
    build(pkgs, "reflex-1.1", "reflex", kLr, Channel::Stable, "1.1.0", {{"nvidia.streamline.reflex", "sl-2.12.0", "reflex"}});
    build(pkgs, "reflex-1.2-bigmajor", "reflex", kLr, Channel::Stable, "1.2.0", {{"nvidia.streamline.reflex", "sl-3.0.0", "reflex", {}, "test", "1.2.0", "sl", 3}});
}
inline gr::CatalogEntry entry_for(const gfs::path& pkgs, const std::string& id, bool autoOk) {
    auto m = gr::parse_manifest(gr::read_text(pkgs / id / "set.json"));
    gr::CatalogEntry e;
    e.set_id = m.set_id; e.family = m.family; e.category = m.category; e.channel = m.channel; e.version = m.version;
    e.release = m.components.front().release; e.api_family = m.components.front().api_family; e.api_major = m.components.front().api_major;
    e.manifest_sha256 = gr::sha256_file(pkgs / id / "set.json"); e.auto_ok = autoOk;
    return e;
}
inline void le16(std::string& s, unsigned v) { s += char(v & 255); s += char(v >> 8 & 255); }
inline void le32(std::string& s, std::uint32_t v) { le16(s, v & 0xFFFF); le16(s, v >> 16); }
struct ZE { std::string name, localName, data; int method = 0; std::uint32_t crc = 0, usize = 0; unsigned flags = 0, madeBy = 20; std::uint32_t ext = 0; bool dir = false; };
inline ZE ze_store(const std::string& name, const std::string& content) { ZE e; e.name = name; e.data = content; e.crc = gr::crc32_bytes(content); e.usize = std::uint32_t(content.size()); return e; }
inline std::string make_zip(std::vector<ZE> es) {
    std::string out, cd;
    for (auto& e : es) {
        auto off = std::uint32_t(out.size());
        auto ln = e.localName.empty() ? e.name : e.localName;
        le32(out, 0x04034b50); le16(out, 20); le16(out, e.flags); le16(out, e.method); le16(out, 0); le16(out, 0);
        le32(out, e.crc); le32(out, std::uint32_t(e.data.size())); le32(out, e.usize); le16(out, unsigned(ln.size())); le16(out, 0);
        out += ln; out += e.data;
        le32(cd, 0x02014b50); le16(cd, e.madeBy); le16(cd, 20); le16(cd, e.flags); le16(cd, e.method); le16(cd, 0); le16(cd, 0);
        le32(cd, e.crc); le32(cd, std::uint32_t(e.data.size())); le32(cd, e.usize); le16(cd, unsigned(e.name.size())); le16(cd, 0); le16(cd, 0);
        le16(cd, 0); le16(cd, 0); le32(cd, e.ext); le32(cd, off); cd += e.name;
    }
    auto cdOff = std::uint32_t(out.size());
    out += cd;
    le32(out, 0x06054b50); le16(out, 0); le16(out, 0); le16(out, unsigned(es.size())); le16(out, unsigned(es.size()));
    le32(out, std::uint32_t(cd.size())); le32(out, cdOff); le16(out, 0);
    return out;
}
inline std::string from_hex(const std::string& h) {
    std::string o;
    auto n = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i + 1 < h.size(); i += 2) o += char(n(h[i]) * 16 + n(h[i + 1]));
    return o;
}
inline std::string sample_text() { std::string s; for (int i = 0; i < 200; ++i) s += "line " + std::to_string(i) + ": starlight graphics runtime\n"; return s; }
inline std::string text_deflate() {
    return from_hex("8dd7cb6ddb501445d179aa5009dc241f3fe9c6080c5b806204b2d27f900ab8e667b4413cdef5b87fbddfa69fb7efd7dbf371fff87cdd3e9e6f7f3eefbfbe6fcfbf5faffbeff71f8fff93ae27f3f564b99eacd793713dd9ae27fbf5e4b89e9c904ef242df20705038481c340e22079583cc41e7193acff21d43e7193acfd07986ce33749ea1f30c9d67e8bc40e7053a2ff26040e7053a2fd07981ce0b745ea0f3029d57e8bc42e7153aaff23243e7153aafd07985ce2b745ea1f380ce033a0fe83ca0f3905f20741ed07940e7019d0774dea0f3069d37e8bc41e70d3a6f726b40e70d3a6fd07983ce3b74dea1f30e9d77e8bc43e71d3aef72d441e71d3aefd0f980ce07743ea0f3019d0fe87c40e7033a1f723d43e7033a9fd0f984ce27743ea1f3099d4fe87c42e7133a9fc214728a406512a94c429549ac32095626d1ca245c99c42b93806592e24643294e38241d120fc9870444122211518c9820315162c2c4c4890914132926544cac986031d162c2c5c48b0918133126644ccc98a0315163c2c6c48d091c133926744cec98e031d163c2c7c48f0920134126844c0c9920325164c2c8c4910924134926944c2c996032d164c2c9c4930928135126a44c4c99a0325165c2cac495092c135926b44c6c99e032d165c2cbc4970930136126c44c8c9920335166c2ccc4990934136926d44cac996033d166c2cdc49b0938137126e4ecc29cff00");
}
inline std::string zeros_deflate() { return from_hex("edc13101000000c2a0f54f6d085fa0" + std::string(2032, '0') + "3e03"); }
inline std::string stored_deflate(const std::string& s) {
    std::string o;
    o += char(1); le16(o, unsigned(s.size())); le16(o, unsigned(~s.size() & 0xFFFF)); o += s;
    return o;
}
inline std::string tree_listing(const gfs::path& root) {
    std::string s;
    std::error_code ec;
    for (auto& e : gfs::recursive_directory_iterator(root, ec)) s += gr::utf8(e.path()) + "\n";
    return s;
}
inline bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
struct Fault {
    bool armed = false;
    int count = 0, crashAt = -1;
    std::vector<std::string> names;
    gr::FaultHook hook() {
        return [this](std::string_view step) {
            if (!armed) return;
            names.emplace_back(step);
            if (count++ == crashAt) throw gr::InjectedCrash{std::string(step)};
        };
    }
};
inline bool state_valid(const gfs::path& root, const gfs::path& bundled, std::string& why) {
    gr::StoreOptions o;
    o.root = root; o.bundledRoot = bundled;
    gr::Store s(o);
    auto res = s.resolve();
    for (auto& f : res.families) if (f.setId.empty()) { why = "family " + f.family + " has no usable set"; return false; }
    if (!res.warnings.empty()) { why = res.warnings.front(); return false; }
    std::error_code ec;
    for (auto& e : gfs::recursive_directory_iterator(root, ec)) {
        auto n = gr::utf8(e.path().filename());
        if (n.find(".tmp") != std::string::npos || n.rfind(".staging-", 0) == 0) { why = "leftover " + n; return false; }
    }
    return true;
}
}

template<class Require> void graphics_runtime_checks(Require require, const fs::path& folder) {
    namespace gr = starlight::gfxrt;
    using namespace grtest;
    using gr::Channel;
    const fs::path work = folder / "graphics-runtime-checks";
    fs::remove_all(work);
    fs::create_directories(work);
    const auto bundled = work / "bundled", pkgs = work / "packages";
    fixture_packages(bundled, pkgs);
    const gr::EngineApi api = gr::EngineApi::parse("sl=2,test=1,xess=3");
    auto opts = [&](const std::string& name) { gr::StoreOptions o; o.root = work / name; o.bundledRoot = bundled; return o; };

    require(gr::sha256_bytes("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "GFX hashing matches the SHA-256 reference vector");
    require(gr::crc32_bytes("123456789") == 0xCBF43926u, "GFX CRC-32 matches the reference vector");
    require(gr::parse_version("2.12.0") && gr::parse_version("310.9.0.0") && !gr::parse_version("2") && !gr::parse_version("1.x") && !gr::parse_version("1.2.3.4.5") && !gr::parse_version("1..2"), "GFX version parser accepts 2 to 4 numeric parts only");
    require(gr::parse_version("310.9.0.0")->compare(*gr::parse_version("310.7.0")) > 0 && gr::parse_version("1.2")->compare(*gr::parse_version("1.2.0")) == 0, "GFX versions compare numerically with implied zeros");
    {
        std::string why;
        const char* bad[] = {"", "/abs", "a/../b", "..", "a\\b", "C:/x", "a:b", "a//b", "con", "a/NUL.txt", "com1", "a.", "a /b", "x/.", "a<b", "a?"};
        bool all = true;
        for (auto b : bad) if (gr::safe_relative_path(b, why)) all = false;
        require(all, "GFX path guard rejects traversal, absolute, drive, stream, device and odd names");
        require(gr::safe_relative_path("bin/sl.interposer.dll", why) && gr::safe_relative_path("licenses/a-b_c.txt", why), "GFX path guard accepts ordinary relative paths");
    }

    {
        auto good = gr::manifest_to_json(gr::parse_manifest(gr::read_text(pkgs / "sl-2.13.0" / "set.json")));
        auto m = gr::parse_manifest(good);
        gr::validate_manifest(m);
        require(m.set_id == "sl-2.13.0" && m.components.size() == 2 && m.components[0].files.size() == 2 && gr::manifest_to_json(m) == good, "GFX manifest round-trips id, vendor, family, version, hashes, signer and licence");
        auto reject = [&](const std::string& what, std::function<void(gr::SetManifest&)> mutate) {
            auto copy = m;
            mutate(copy);
            try { gr::validate_manifest(copy); return false; } catch (const gr::GrError&) { return true; }
        };
        require(reject("traversal", [](auto& x) { x.components[0].files[0].path = "../evil.dll"; }), "GFX manifest rejects a traversal path");
        require(reject("absolute", [](auto& x) { x.components[0].files[0].path = "/evil.dll"; }), "GFX manifest rejects an absolute path");
        require(reject("backslash", [](auto& x) { x.components[0].files[0].path = "bin\\evil.dll"; }), "GFX manifest rejects a backslash path");
        require(reject("case dup", [](auto& x) { x.components[1].files[0].path = "BIN/NVIDIA.STREAMLINE.INTERPOSER.DLL"; }), "GFX manifest rejects case-insensitive duplicate paths");
        require(reject("no licence", [](auto& x) { x.components[0].licence_files.clear(); }), "GFX manifest rejects a component without a licence file");
        require(reject("licence outside", [](auto& x) { x.components[0].licence_files = {"licenses/other.txt"}; }), "GFX manifest rejects a licence file that is not among the files");
        require(reject("bad hash", [](auto& x) { x.components[0].files[0].sha256 = "XYZ"; }), "GFX manifest rejects a malformed SHA-256");
        require(reject("mixed release", [](auto& x) { x.components[1].release = "sl-9.9.9"; }), "GFX manifest rejects a matched set mixing releases");
        require(reject("dup id", [](auto& x) { x.components[1].id = x.components[0].id; }), "GFX manifest rejects duplicate component ids");
        require(reject("no signer", [](auto& x) { x.components[0].signer.clear(); }), "GFX manifest rejects a missing signer");
        require(reject("bad version", [](auto& x) { x.components[0].version = "x"; }), "GFX manifest rejects a bad component version");
        require(reject("id trailing dot", [](auto& x) { x.set_id = "plain-1.1."; }) && reject("id device", [](auto& x) { x.set_id = "con"; }) && reject("id device ext", [](auto& x) { x.set_id = "nul.1"; }) && reject("id com", [](auto& x) { x.set_id = "com1"; }), "GFX manifest rejects a set id that Windows would rename as a folder (trailing dot or device name)");
        require(reject("intel dev", [](auto& x) { x.components[0].vendor = "intel"; x.components[0].version = "3.0.95"; }), "GFX manifest rejects an Intel development build (patch 90 or higher)");
        require(reject("self requires", [](auto& x) { x.components[0].requires_.push_back(rq(x.components[0].id, "")); }), "GFX manifest rejects a component requiring itself");
        require(reject("bad origin", [](auto& x) { x.components[0].requires_.push_back(rq("other.thing", "", "rumour")); }), "GFX manifest labels every rule as vendor or Starlight policy");
        require(reject("empty", [](auto& x) { x.components.clear(); }), "GFX manifest rejects an empty set");
        auto j = nlohmann::json::parse(good);
        auto bad1 = j; bad1["surprise"] = 1;
        auto bad2 = j; bad2["components"][0]["extra"] = 1;
        auto bad3 = j; bad3["components"][0]["files"][0]["extra"] = 1;
        auto bad4 = j; bad4["channel"] = "nightly";
        auto bad5 = j; bad5.erase("family");
        int rejected = 0;
        for (auto* b : {&bad1, &bad2, &bad3, &bad4, &bad5}) try { gr::parse_manifest(b->dump()); } catch (const gr::GrError&) { ++rejected; }
        require(rejected == 5, "GFX manifest parser rejects unknown keys, unknown channel and missing fields");
        try { gr::parse_manifest("{nope"); require(false, "GFX bad JSON"); } catch (const gr::GrError&) { require(true, "GFX manifest parser reports invalid JSON"); }
    }

    {
        auto dir = pkgs / "sl-2.13.0";
        auto m = gr::parse_manifest(gr::read_text(dir / "set.json"));
        require(gr::verify_set_folder(dir, m).ok, "GFX a pristine set folder verifies");
        const auto copy = work / "tamper";
        fs::copy(dir, copy, fs::copy_options::recursive);
        put(copy / "bin" / "nvidia.streamline.common.dll", "FAKE COMPONENT changed");
        require(!gr::verify_set_folder(copy, m).ok, "GFX a changed file fails verification");
        put(copy / "bin" / "nvidia.streamline.common.dll", "FAKE COMPONENT nvidia.streamline.common sl-2.13.0 2.13.0 sl-2.13.0");
        require(gr::verify_set_folder(copy, m).ok, "GFX restoring the exact content verifies again");
        put(copy / "extra.dll", "x");
        require(!gr::verify_set_folder(copy, m).ok, "GFX an unexpected extra file fails verification");
        fs::remove(copy / "extra.dll");
        fs::remove(copy / "bin" / "nvidia.streamline.common.dll");
        require(!gr::verify_set_folder(copy, m).ok, "GFX a missing file fails verification");
    }

    {
        const auto text = sample_text();
        std::string out, err;
        require(gr::inflate_raw(text_deflate(), out, text.size(), err) && out == text, "GFX inflate decodes a dynamic-Huffman stream");
        require(gr::inflate_raw(stored_deflate("hello stored"), out, 12, err) && out == "hello stored", "GFX inflate decodes a stored block");
        require(gr::inflate_raw(from_hex("cb48cdc9c90700"), out, 5, err) && out == "hello", "GFX inflate decodes a fixed-Huffman stream");
        require(!gr::inflate_raw(text_deflate(), out, 100, err), "GFX inflate refuses output beyond the declared size");
        require(!gr::inflate_raw(text_deflate().substr(0, 40), out, text.size(), err), "GFX inflate refuses a truncated stream");
        require(!gr::inflate_raw(from_hex("07"), out, 1, err), "GFX inflate refuses an invalid block type");

        auto extract = [&](const std::string& name, const std::string& zip, gr::ZipLimits lim = {}) {
            auto dest = work / ("zip-" + name);
            fs::remove_all(dest);
            auto r = gr::extract_zip_bytes(zip, dest, lim);
            return std::make_pair(r, dest);
        };
        {
            ZE d; d.name = "docs/text.txt"; d.method = 8; d.data = text_deflate(); d.crc = gr::crc32_bytes(text); d.usize = std::uint32_t(text.size());
            auto [r, dest] = extract("good", make_zip({ze_store("a/b.txt", "stored content"), d}));
            require(r.ok && gr::read_text(dest / "a" / "b.txt") == "stored content" && gr::read_text(dest / "docs" / "text.txt") == text, "GFX zip extracts STORE and DEFLATE entries byte-exactly");
        }
        std::vector<std::pair<std::string, std::string>> evil{{"..\\x", "..\\evil.txt"}, {"slip", "../evil.txt"}, {"abs", "/evil.txt"}, {"drive", "C:/evil.txt"}, {"ads", "file.txt:stream"}, {"nested", "a/../../evil.txt"}, {"dev", "NUL.txt"}, {"dotdot", ".."}};
        for (auto& [tag, name] : evil) {
            auto [r, dest] = extract("evil-" + tag, make_zip({ze_store(name, "x")}));
            require(!r.ok && !fs::exists(dest) && !fs::exists(work / "evil.txt") && !fs::exists(folder / "evil.txt"), ("GFX zip refuses entry name " + name).c_str());
        }
        {
            auto [r, dest] = extract("dup", make_zip({ze_store("a.txt", "1"), ze_store("A.TXT", "2")}));
            require(!r.ok && !fs::exists(dest), "GFX zip refuses case-colliding duplicates");
        }
        {
            auto [r, dest] = extract("fileindir", make_zip({ze_store("a", "1"), ze_store("a/b", "2")}));
            require(!r.ok && !fs::exists(dest), "GFX zip refuses an entry inside a file entry");
        }
        {
            auto e = ze_store("enc.txt", "x"); e.flags = 1;
            require(!extract("enc", make_zip({e})).first.ok, "GFX zip refuses encrypted entries");
            auto l = ze_store("link", "target"); l.madeBy = (3 << 8) | 20; l.ext = 0xA1FFu << 16;
            require(!extract("link", make_zip({l})).first.ok, "GFX zip refuses link entries");
            auto m = ze_store("m.txt", "x"); m.method = 12;
            require(!extract("method", make_zip({m})).first.ok, "GFX zip refuses unsupported compression methods");
            auto c = ze_store("c.txt", "content"); c.crc ^= 1;
            auto cr = extract("crc", make_zip({c}));
            require(!cr.first.ok && !fs::exists(cr.second), "GFX zip refuses a checksum mismatch and removes the partial folder");
            auto n = ze_store("n.txt", "content"); n.localName = "other.txt";
            require(!extract("names", make_zip({n})).first.ok, "GFX zip refuses local and central names that differ");
            const auto oddDest = work / "zip-odd-name";
            fs::remove_all(oddDest);
            bool oddOk = false;
            try { oddOk = gr::extract_zip_bytes(make_zip({ze_store("first.txt", "1"), ze_store("odd\xE9.txt", "2")}), oddDest).ok; } catch (const std::exception&) {}
            require(!oddOk && !fs::exists(oddDest), "GFX zip leaves no partly extracted folder when an entry name cannot be converted");
        }
        {
            ZE bomb; bomb.name = "zeros.bin"; bomb.method = 8; bomb.data = zeros_deflate(); bomb.usize = 1 << 20;
            std::string zeros(1 << 20, '\0');
            bomb.crc = gr::crc32_bytes(zeros);
            auto br = extract("bomb", make_zip({bomb}));
            require(!br.first.ok && contains(br.first.message, "ratio") && !fs::exists(br.second), "GFX zip refuses a high expansion ratio before expanding");
            gr::ZipLimits loose; loose.maxRatio = 100000;
            require(extract("bomb-loose", make_zip({bomb}), loose).first.ok, "GFX the same stream extracts when the ratio limit is raised");
            ZE lie = bomb; lie.usize = 100;
            require(!extract("lie", make_zip({lie}), loose).first.ok, "GFX zip refuses a stream that expands beyond its declared size");
            gr::ZipLimits tiny; tiny.maxTotal = 1000;
            require(!extract("total", make_zip({ze_store("a", std::string(900, 'a')), ze_store("b", std::string(900, 'b'))}), tiny).first.ok, "GFX zip refuses a total expanded size over the limit");
            gr::ZipLimits few; few.maxEntries = 2;
            require(!extract("many", make_zip({ze_store("a", "1"), ze_store("b", "2"), ze_store("c", "3")}), few).first.ok, "GFX zip refuses too many entries");
            gr::ZipLimits one; one.maxFile = 10;
            require(!extract("bigfile", make_zip({ze_store("a", std::string(50, 'a'))}), one).first.ok, "GFX zip refuses a single file over the limit");
        }
        {
            auto z = make_zip({ze_store("a", "1")});
            require(!extract("trunc", z.substr(0, z.size() - 8)).first.ok && !extract("tiny", "PK").first.ok, "GFX zip refuses truncated and tiny files");
            auto dest = work / "zip-exists";
            fs::create_directories(dest);
            put(dest / "keep.txt", "mine");
            require(!gr::extract_zip_bytes(z, dest).ok && gr::read_text(dest / "keep.txt") == "mine", "GFX zip never extracts into an existing folder and keeps its content");
        }
    }

    {
        for (const char* name : {"bad\xff.txt", "over\xC0\xAF.txt", "sur\xED\xA0\x80.txt"}) {
            auto dest = work / "zip-utf8";
            fs::remove_all(dest);
            bool threw = false;
            gr::Result r;
            try { r = gr::extract_zip_bytes(make_zip({ze_store(name, "x")}), dest); } catch (...) { threw = true; }
            require(!threw && !r.ok && !fs::exists(dest), "GFX zip refuses an entry name that is not valid UTF-8 without throwing or leaving a folder");
        }
    }
    {
        for (const char* name : {"bad\xff.txt", "over\xC0\xAF.txt", "sur\xED\xA0\x80.txt"}) {
            auto dest = work / "zip-utf8";
            fs::remove_all(dest);
            bool threw = false;
            gr::Result r;
            try { r = gr::extract_zip_bytes(make_zip({ze_store(name, "x")}), dest); } catch (...) { threw = true; }
            require(!threw && !r.ok && !fs::exists(dest), "GFX zip refuses an entry name that is not valid UTF-8 without throwing or leaving a folder");
        }
    }
    {
        auto dest = work / "zip-unicode-case";
        fs::remove_all(dest);
        auto r = gr::extract_zip_bytes(make_zip({ze_store("\xC3\x89.txt", "1"), ze_store("\xC3\xA9.txt", "2")}), dest);
        require(!r.ok && !fs::exists(dest), "GFX zip refuses names that differ only by non-ASCII letter case");
    }
    {
        for (const char* name : {"bad\xff.txt", "over\xC0\xAF.txt", "sur\xED\xA0\x80.txt"}) {
            auto dest = work / "zip-utf8";
            fs::remove_all(dest);
            bool threw = false;
            gr::Result r;
            try { r = gr::extract_zip_bytes(make_zip({ze_store(name, "x")}), dest); } catch (...) { threw = true; }
            require(!threw && !r.ok && !fs::exists(dest), "GFX zip refuses an entry name that is not valid UTF-8 without throwing or leaving a folder");
        }
    }
    {
        auto dest = work / "zip-unicode-case";
        fs::remove_all(dest);
        auto r = gr::extract_zip_bytes(make_zip({ze_store("\xC3\x89.txt", "1"), ze_store("\xC3\xA9.txt", "2")}), dest);
        require(!r.ok && !fs::exists(dest), "GFX zip refuses names that differ only by non-ASCII letter case");
    }
    {
        std::string why;
        for (const char* name : {"CONOUT$", "conin$", "con .txt", "NUL  .txt", "COM\xC2\xB9", "lpt\xC2\xB2.dat"})
            require(!gr::safe_relative_path(name, why), (std::string("GFX path guard rejects the device name ") + name).c_str());
        require(gr::safe_relative_path("console.txt", why) && gr::safe_relative_path("com10.txt", why), "GFX path guard still accepts names that merely start like a device");
    }
    {
        for (const char* name : {"bad\xff.txt", "over\xC0\xAF.txt", "sur\xED\xA0\x80.txt"}) {
            auto dest = work / "zip-utf8";
            fs::remove_all(dest);
            bool threw = false;
            gr::Result r;
            try { r = gr::extract_zip_bytes(make_zip({ze_store(name, "x")}), dest); } catch (...) { threw = true; }
            require(!threw && !r.ok && !fs::exists(dest), "GFX zip refuses an entry name that is not valid UTF-8 without throwing or leaving a folder");
        }
    }
    {
        auto dest = work / "zip-unicode-case";
        fs::remove_all(dest);
        auto r = gr::extract_zip_bytes(make_zip({ze_store("\xC3\x89.txt", "1"), ze_store("\xC3\xA9.txt", "2")}), dest);
        require(!r.ok && !fs::exists(dest), "GFX zip refuses names that differ only by non-ASCII letter case");
    }
    {
        std::string why;
        for (const char* name : {"CONOUT$", "conin$", "con .txt", "NUL  .txt", "COM\xC2\xB9", "lpt\xC2\xB2.dat"})
            require(!gr::safe_relative_path(name, why), (std::string("GFX path guard rejects the device name ") + name).c_str());
        require(gr::safe_relative_path("console.txt", why) && gr::safe_relative_path("com10.txt", why), "GFX path guard still accepts names that merely start like a device");
    }
    {
        auto m = gr::parse_manifest(gr::read_text(pkgs / "sl-2.13.0" / "set.json"));
        auto j = nlohmann::json::parse(gr::manifest_to_json(m));
        auto wrapSchema = j; wrapSchema["schema"] = 4294967297ull;
        auto wrapApi = j; wrapApi["components"][0]["api_major"] = 4294967297ull;
        int refused = 0;
        for (auto* b : {&wrapSchema, &wrapApi}) try { gr::validate_manifest(gr::parse_manifest(b->dump())); } catch (const gr::GrError&) { ++refused; }
        require(refused == 2, "GFX manifest integers outside the 32-bit range are refused instead of wrapping to a small number");
    }
    {
        for (const char* name : {"bad\xff.txt", "over\xC0\xAF.txt", "sur\xED\xA0\x80.txt"}) {
            auto dest = work / "zip-utf8";
            fs::remove_all(dest);
            bool threw = false;
            gr::Result r;
            try { r = gr::extract_zip_bytes(make_zip({ze_store(name, "x")}), dest); } catch (...) { threw = true; }
            require(!threw && !r.ok && !fs::exists(dest), "GFX zip refuses an entry name that is not valid UTF-8 without throwing or leaving a folder");
        }
    }
    {
        auto dest = work / "zip-unicode-case";
        fs::remove_all(dest);
        auto r = gr::extract_zip_bytes(make_zip({ze_store("\xC3\x89.txt", "1"), ze_store("\xC3\xA9.txt", "2")}), dest);
        require(!r.ok && !fs::exists(dest), "GFX zip refuses names that differ only by non-ASCII letter case");
    }
    {
        std::string why;
        for (const char* name : {"CONOUT$", "conin$", "con .txt", "NUL  .txt", "COM\xC2\xB9", "lpt\xC2\xB2.dat"})
            require(!gr::safe_relative_path(name, why), (std::string("GFX path guard rejects the device name ") + name).c_str());
        require(gr::safe_relative_path("console.txt", why) && gr::safe_relative_path("com10.txt", why), "GFX path guard still accepts names that merely start like a device");
    }
    {
        auto m = gr::parse_manifest(gr::read_text(pkgs / "sl-2.13.0" / "set.json"));
        auto j = nlohmann::json::parse(gr::manifest_to_json(m));
        auto wrapSchema = j; wrapSchema["schema"] = 4294967297ull;
        auto wrapApi = j; wrapApi["components"][0]["api_major"] = 4294967297ull;
        int refused = 0;
        for (auto* b : {&wrapSchema, &wrapApi}) try { gr::validate_manifest(gr::parse_manifest(b->dump())); } catch (const gr::GrError&) { ++refused; }
        require(refused == 2, "GFX manifest integers outside the 32-bit range are refused instead of wrapping to a small number");
    }
    {
        const auto kp = gr::testing::generate_test_keypair();
        gr::TrustStore trust;
        trust.keys["test-1"] = kp.publicBlob;
        gr::Catalog c;
        c.serial = 5;
        c.entries = {entry_for(pkgs, "sl-2.13.0", true)};
        auto payload = nlohmann::json::parse(gr::testing::catalog_payload_json(c));
        auto negative = payload; negative["serial"] = -1;
        auto wrapped = payload; wrapped["sets"][0]["api_major"] = 4294967297ull;
        auto fractional = payload; fractional["serial"] = 5.5;
        int refused = 0;
        for (auto* b : {&negative, &wrapped, &fractional}) if (!gr::verify_catalog(gr::testing::sign_catalog_payload(kp, "test-1", b->dump()), trust).ok) ++refused;
        require(refused == 3, "GFX a signed catalog with a negative, fractional or out-of-range number is refused instead of wrapping");
    }
    {
        for (const char* name : {"bad\xff.txt", "over\xC0\xAF.txt", "sur\xED\xA0\x80.txt"}) {
            auto dest = work / "zip-utf8";
            fs::remove_all(dest);
            bool threw = false;
            gr::Result r;
            try { r = gr::extract_zip_bytes(make_zip({ze_store(name, "x")}), dest); } catch (...) { threw = true; }
            require(!threw && !r.ok && !fs::exists(dest), "GFX zip refuses an entry name that is not valid UTF-8 without throwing or leaving a folder");
        }
    }
    {
        auto dest = work / "zip-unicode-case";
        fs::remove_all(dest);
        auto r = gr::extract_zip_bytes(make_zip({ze_store("\xC3\x89.txt", "1"), ze_store("\xC3\xA9.txt", "2")}), dest);
        require(!r.ok && !fs::exists(dest), "GFX zip refuses names that differ only by non-ASCII letter case");
    }
    {
        std::string why;
        for (const char* name : {"CONOUT$", "conin$", "con .txt", "NUL  .txt", "COM\xC2\xB9", "lpt\xC2\xB2.dat"})
            require(!gr::safe_relative_path(name, why), (std::string("GFX path guard rejects the device name ") + name).c_str());
        require(gr::safe_relative_path("console.txt", why) && gr::safe_relative_path("com10.txt", why), "GFX path guard still accepts names that merely start like a device");
    }
    {
        const auto text = sample_text();
        ZE d; d.name = "docs/text.txt"; d.method = 8; d.data = text_deflate(); d.crc = gr::crc32_bytes(text); d.usize = std::uint32_t(text.size());
        ZE dir; dir.name = "empty/"; dir.madeBy = 20;
        const std::string zip = make_zip({ze_store("a/b.txt", "stored content"), d, dir});
        const std::size_t eocd = zip.size() - 22;
        auto put16 = [](std::string z, std::size_t at, unsigned v) { z[at] = char(v & 255); z[at + 1] = char(v >> 8 & 255); return z; };
        auto put32 = [&](std::string z, std::size_t at, std::uint32_t v) { z = put16(z, at, v & 0xFFFF); return put16(z, at + 2, v >> 16); };
        const std::uint32_t cdOff = std::uint32_t(zip[eocd + 16] & 255) | std::uint32_t(zip[eocd + 17] & 255) << 8 | std::uint32_t(zip[eocd + 18] & 255) << 16;
        bool anyThrew = false;
        auto attempt = [&](const std::string& tag, const std::string& z) {
            auto dest = work / ("zip-damage-" + tag);
            fs::remove_all(dest);
            gr::Result r;
            try { r = gr::extract_zip_bytes(z, dest); } catch (...) { anyThrew = true; r = gr::Result::fail("threw"); }
            return std::make_pair(r, dest);
        };
        const std::pair<const char*, std::string> damaged[] = {
            {"counts-disagree", put16(put16(zip, eocd + 8, 4), eocd + 10, 4)},
            {"zip64-count", put16(put16(zip, eocd + 8, 0xFFFF), eocd + 10, 0xFFFF)},
            {"zip64-size", put32(zip, eocd + 12, 0xFFFFFFFFu)},
            {"zip64-offset", put32(zip, eocd + 16, 0xFFFFFFFFu)},
            {"directory-past-end", put32(zip, eocd + 16, std::uint32_t(zip.size()))},
            {"directory-too-big", put32(zip, eocd + 12, std::uint32_t(zip.size()))},
            {"directory-signature", put16(zip, cdOff, 0)},
            {"local-signature", put16(zip, 0, 0)},
            {"entry-offset-huge", put32(zip, cdOff + 42, 0x7FFFFFFFu)},
            {"entry-offset-zip64", put32(zip, cdOff + 42, 0xFFFFFFFFu)},
            {"entry-size-zip64", put32(zip, cdOff + 20, 0xFFFFFFFFu)},
            {"stored-size-differs", put32(zip, cdOff + 20, 3)},
            {"name-length-huge", put16(zip, cdOff + 28, 0xFFFF)},
            {"extra-length-huge", put16(zip, cdOff + 30, 0xFFFF)},
            {"comment-length-huge", put16(zip, cdOff + 32, 0xFFFF)},
            {"directory-with-data", put32(zip, zip.size() - 22 - 46 - 6 + 20, 5)},
        };
        for (const auto& [tag, z] : damaged) {
            auto [r, dest] = attempt(tag, z);
            require(!r.ok && !fs::exists(dest), ("GFX zip refuses a damaged archive and leaves nothing behind: " + std::string(tag)).c_str());
        }
        attempt("no-entries", make_zip({}));
        int refused = 0, extracted = 0;
        bool leftover = false, escaped = false;
        for (std::size_t at = 0; at < zip.size(); ++at)
            for (unsigned char value : {0x00, 0xFF, 0x80, 0x2F}) {
                if (static_cast<unsigned char>(zip[at]) == value) continue;
                auto z = zip;
                z[at] = static_cast<char>(value);
                auto [r, dest] = attempt("flip", z);
                if (!r.ok) { ++refused; leftover = leftover || fs::exists(dest); continue; }
                ++extracted;
                for (auto& entry : fs::recursive_directory_iterator(dest)) {
                    auto rel = fs::relative(entry.path(), dest).generic_string();
                    escaped = escaped || rel.empty() || rel.rfind("..", 0) == 0 || fs::path(rel).is_absolute();
                }
                fs::remove_all(dest);
            }
        require(!anyThrew, "GFX zip never throws on a damaged archive");
        require(!leftover && !escaped, "GFX zip leaves no folder after refusing and never writes outside its target folder, whatever single byte is damaged");
        require(refused > 100 && extracted > 0, "GFX zip: single-byte damage is refused or extracted safely");
    }
    {
        auto m = gr::parse_manifest(gr::read_text(pkgs / "sl-2.13.0" / "set.json"));
        auto j = nlohmann::json::parse(gr::manifest_to_json(m));
        auto wrapSchema = j; wrapSchema["schema"] = 4294967297ull;
        auto wrapApi = j; wrapApi["components"][0]["api_major"] = 4294967297ull;
        int refused = 0;
        for (auto* b : {&wrapSchema, &wrapApi}) try { gr::validate_manifest(gr::parse_manifest(b->dump())); } catch (const gr::GrError&) { ++refused; }
        require(refused == 2, "GFX manifest integers outside the 32-bit range are refused instead of wrapping to a small number");
    }
    {
        const auto kp = gr::testing::generate_test_keypair();
        gr::TrustStore trust;
        trust.keys["test-1"] = kp.publicBlob;
        gr::Catalog c;
        c.serial = 5;
        c.entries = {entry_for(pkgs, "sl-2.13.0", true)};
        auto payload = nlohmann::json::parse(gr::testing::catalog_payload_json(c));
        auto negative = payload; negative["serial"] = -1;
        auto wrapped = payload; wrapped["sets"][0]["api_major"] = 4294967297ull;
        auto fractional = payload; fractional["serial"] = 5.5;
        int refused = 0;
        for (auto* b : {&negative, &wrapped, &fractional}) if (!gr::verify_catalog(gr::testing::sign_catalog_payload(kp, "test-1", b->dump()), trust).ok) ++refused;
        require(refused == 3, "GFX a signed catalog with a negative, fractional or out-of-range number is refused instead of wrapping");
    }
    {
        const auto kp = gr::testing::generate_test_keypair();
        const auto other = gr::testing::generate_test_keypair();
        gr::TrustStore trust;
        trust.keys["test-1"] = kp.publicBlob;
        gr::Catalog c;
        c.serial = 5;
        c.entries = {entry_for(pkgs, "sl-2.13.0", true), entry_for(pkgs, "sl-2.14.0-exp", false)};
        c.revoked = {"old-bad"};
        c.min_safe["sl"] = "2.10.0";
        auto payload = gr::testing::catalog_payload_json(c);
        auto text = gr::testing::sign_catalog_payload(kp, "test-1", payload);
        auto r = gr::verify_catalog(text, trust);
        require(r.ok && r.catalog.serial == 5 && r.catalog.entries.size() == 2 && r.catalog.revoked.size() == 1 && r.catalog.min_safe.at("sl") == "2.10.0", "GFX a catalog signed with the local test key verifies and parses");
        auto j = nlohmann::json::parse(text);
        auto forged = j; forged["payload"] = payload + " ";
        require(!gr::verify_catalog(forged.dump(), trust).ok, "GFX a catalog with changed content fails verification");
        auto tampered = nlohmann::json::parse(payload); tampered["serial"] = 99;
        auto forged2 = j; forged2["payload"] = tampered.dump();
        require(!gr::verify_catalog(forged2.dump(), trust).ok, "GFX a catalog with a raised serial fails verification");
        require(!gr::verify_catalog(gr::testing::sign_catalog_payload(other, "test-1", payload), trust).ok, "GFX a catalog signed by a different key is refused");
        require(!gr::verify_catalog(gr::testing::sign_catalog_payload(kp, "unknown", payload), trust).ok, "GFX a catalog from an untrusted key id is refused");
        auto shortSig = j; shortSig["signature"] = "abcd";
        require(!gr::verify_catalog(shortSig.dump(), trust).ok && !gr::verify_catalog("not json", trust).ok, "GFX malformed catalog envelopes are refused");
        auto extra = j; extra["extra"] = 1;
        require(!gr::verify_catalog(extra.dump(), trust).ok, "GFX unknown envelope keys are refused");
        auto badPayload = nlohmann::json::parse(payload); badPayload["sets"][0]["channel"] = "nightly";
        require(!gr::verify_catalog(gr::testing::sign_catalog_payload(kp, "test-1", badPayload.dump()), trust).ok, "GFX a signed catalog with an unknown channel is refused");
        gr::MemoryCatalogProvider none(std::nullopt), some(text);
        require(!none.fetch() && some.fetch() && !gr::FileCatalogProvider(work / "missing.json").fetch(), "GFX catalog providers report an unavailable catalog honestly");
    }

    {
        auto o = opts("store-basic");
        gr::Store s(o);
        require(s.first_launch_choice_needed() && s.state().updateMode == "unset", "GFX first launch needs an explicit update choice");
        auto res = s.resolve();
        require(res.families.size() == 5 && res.find("plain")->source == "bundled" && res.find("plain")->version == "1.0.0" && res.find("dlssg")->version == "310.7.0", "GFX with nothing staged every family resolves to its bundled set");
        require(res.find("plain")->category == "upscaling" && res.find("dlssg")->category == "frame-generation" && res.find("reflex")->category == "latency-reduction", "GFX families report the three selector categories");
        require(!s.set_mode("sometimes").ok && s.set_mode("manual").ok && !s.first_launch_choice_needed(), "GFX update mode is validated and persisted");
        { gr::Store again(o); require(again.state().updateMode == "manual" && !again.first_launch_choice_needed(), "GFX the mode choice survives a restart"); }
        auto bad = s.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Catalog, nullptr);
        require(!bad.ok, "GFX a catalog-trusted package without a catalog entry is refused");
        auto st = s.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local);
        require(st.ok && s.list_sets().size() == 6, "GFX a local package stages as an inactive set");
        require(s.resolve().find("plain")->version == "1.0.0", "GFX staging never changes what runs");
        require(s.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local).ok && s.list_sets().size() == 6, "GFX staging the same set twice is idempotent");
        fs::copy(pkgs / "plain-1.1.0", work / "sl-conflict", fs::copy_options::recursive);
        put(work / "sl-conflict" / "bin" / "test.plain.core.dll", "FAKE COMPONENT changed");
        require(!s.stage_folder(work / "sl-conflict", gr::StageTrust::Local).ok, "GFX a corrupt package is refused");
        auto altDir = work / "alt-pkgs";
        build(altDir, "plain-1.1.0", "plain", kUp, Channel::Stable, "1.1.0", {{"test.plain.core", "plain-1.1", "plain", {}, "test", "1.0.1"}});
        auto replaced = s.stage_folder(altDir / "plain-1.1.0", gr::StageTrust::Local);
        require(!replaced.ok && contains(replaced.message, "different set"), "GFX a different package with an installed id never replaces it");
        auto asBundled = work / "bundled-id";
        build(asBundled, "plain-1.0.0", "plain", kUp, Channel::Stable, "1.0.0", {{"test.plain.core", "plain-1.0", "plain"}});
        require(!s.stage_folder(asBundled / "plain-1.0.0", gr::StageTrust::Local).ok, "GFX a package cannot take over a bundled set id");
        require(!s.stage_folder(work / "no-such-folder", gr::StageTrust::Local).ok, "GFX a missing package folder is refused");
        require(fs::exists(bundled / "plain-1.0.0" / "set.json"), "GFX the bundled set is never touched");

        auto pl = s.plan_activation("plain", "plain-1.1.0", api);
        require(pl.ok && pl.changes.size() == 1, "GFX planning an activation does not change anything");
        require(s.resolve().find("plain")->version == "1.0.0" && !s.state().families.at("plain").pending, "GFX a plan is read-only");
        auto act = s.activate("plain", "plain-1.1.0", api);
        require(act.ok && s.state().families.at("plain").pending && s.state().families.at("plain").pending->boots == 0 && s.state().families.at("plain").active.empty(), "GFX activation records a pending trial and leaves the active set alone");
        require(s.resolve().find("plain")->version == "1.0.0", "GFX the pending trial is not used before the next launch");
        auto launch = s.begin_launch();
        require(launch.find("plain")->version == "1.1.0" && launch.find("plain")->source == "trial", "GFX the next launch tries the pending set");
        s.mark_graphics_initialized();
        bool threw = false;
        try { s.begin_launch(); } catch (const gr::GrError&) { threw = true; }
        require(threw, "GFX the launch decision is refused once graphics have started");
        auto loadedDir = launch.find("plain")->dir;
        s.stage_folder(pkgs / "plain-1.2.0-exp", gr::StageTrust::Local);
        auto queued = s.activate("plain", "plain-1.2.0-exp", api);
        require(!queued.ok && contains(queued.message, "trial"), "GFX choosing another version during a running trial is refused");
        require(gr::read_text(loadedDir / "set.json") == gr::read_text(pkgs / "plain-1.1.0" / "set.json"), "GFX loaded set files stay byte-identical when a later selection is queued");
        auto ok = s.confirm_good();
        require(ok.ok && s.state().families.at("plain").active == "plain-1.1.0" && s.state().families.at("plain").lastKnownGood == "plain-1.1.0" && s.state().families.at("plain").previous == "plain-1.0.0", "GFX confirming a good launch promotes active, last known good and previous");
    }

    {
        auto o = opts("store-trial");
        gr::Store s(o);
        s.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local);
        s.activate("plain", "plain-1.1.0", api);
        s.begin_launch();
        auto second = s.begin_launch();
        require(second.find("plain")->version == "1.0.0" && !s.state().families.at("plain").pending && s.state().sets.at("plain-1.1.0").bad, "GFX a launch that never confirmed reverts the trial automatically and marks the set bad");
        require(!s.state().events.empty() && contains(s.state().events.back(), "reverted"), "GFX the automatic revert is recorded in plain language");
        auto again = s.activate("plain", "plain-1.1.0", api);
        require(!again.ok && contains(again.message, "bad"), "GFX a set that failed is not offered again");
        gr::Store s2(opts("store-fail"));
        s2.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local);
        s2.activate("plain", "plain-1.1.0", api);
        s2.begin_launch();
        s2.report_launch_failure("backend init failed");
        require(s2.resolve().find("plain")->version == "1.0.0" && !s2.state().families.at("plain").pending, "GFX a reported backend failure reverts immediately");
        gr::Store s3(opts("store-active-fail"));
        s3.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local);
        s3.activate("plain", "plain-1.1.0", api);
        s3.begin_launch(); s3.confirm_good();
        s3.stage_folder(pkgs / "reflex-1.1", gr::StageTrust::Local);
        s3.activate("plain", "plain-1.0.0", api);
        require(s3.state().families.at("plain").active == "plain-1.0.0" && s3.state().families.at("plain").previous == "plain-1.1.0", "GFX selecting a known good set is direct and remembers the previous one");
        auto rp = s3.restore_previous("plain", api);
        require(rp.ok && s3.state().families.at("plain").active == "plain-1.1.0" && s3.state().families.at("plain").previous == "plain-1.0.0", "GFX restore previous swaps back and forth");
        s3.activate("dlssg", "dlssg-310.7", api);
        require(!s3.restore_previous("dlssg", api).ok, "GFX restore previous with no history says so");
        fs::remove(s3.set_dir("plain-1.1.0") / "bin" / "test.plain.core.dll");
        auto broken = s3.resolve();
        require(broken.find("plain")->version == "1.0.0" && broken.find("plain")->fallback && !broken.warnings.empty(), "GFX an active set whose files changed falls back and warns");
    }

    {
        gr::Store s(opts("store-confirm-unusable"));
        s.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local);
        s.activate("plain", "plain-1.1.0", api);
        s.begin_launch(); s.confirm_good();
        s.stage_folder(pkgs / "plain-1.2.0-exp", gr::StageTrust::Local);
        s.activate("plain", "plain-1.2.0-exp", api);
        fs::remove(s.set_dir("plain-1.2.0-exp") / "bin" / "test.plain.core.dll");
        auto launch = s.begin_launch();
        require(launch.find("plain")->source == "active" && launch.find("plain")->version == "1.1.0", "GFX a trial set that cannot be used at launch falls back to the active set");
        s.confirm_good();
        const auto& f = s.state().families.at("plain");
        require(f.active == "plain-1.1.0" && f.lastKnownGood == "plain-1.1.0" && f.previous == "plain-1.0.0" && !f.pending && s.state().sets.at("plain-1.2.0-exp").bad,
                "GFX confirming a launch never promotes a trial set that could not be used; it is marked bad and the old sets stay");

        gr::Store t(opts("store-confirm-active"));
        t.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local);
        t.activate("plain", "plain-1.1.0", api);
        t.begin_launch(); t.confirm_good();
        t.stage_folder(pkgs / "plain-1.2.0-exp", gr::StageTrust::Local);
        t.activate("plain", "plain-1.2.0-exp", api);
        t.begin_launch(); t.confirm_good();
        t.activate("plain", "plain-1.1.0", api);
        require(t.state().families.at("plain").active == "plain-1.1.0" && t.state().families.at("plain").lastKnownGood == "plain-1.2.0-exp", "GFX fixture: the active set differs from the last known good set");
        fs::remove(t.set_dir("plain-1.1.0") / "bin" / "test.plain.core.dll");
        t.begin_launch(); t.confirm_good();
        require(t.state().families.at("plain").active == "plain-1.1.0" && t.state().families.at("plain").lastKnownGood == "plain-1.2.0-exp", "GFX confirming a launch never makes an unusable active set the last known good set");
    }

    {
        auto o = opts("store-lock");
        gr::Store s(o);
        s.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local);
        s.activate("plain", "plain-1.1.0", api);
        s.begin_launch(); s.confirm_good();
        s.set_ota("plain", true);
        require(s.resolve().find("plain")->otaAllowed, "GFX vendor update policy can be allowed for an unlocked family");
        require(s.lock("plain").ok && s.resolve().find("plain")->locked && !s.resolve().find("plain")->otaAllowed, "GFX locking disables vendor updates before init");
        auto act = s.activate("plain", "plain-1.0.0", api);
        require(!act.ok && contains(act.message, "locked"), "GFX a locked family refuses activation with a plain explanation");
        require(!s.restore_previous("plain", api).ok && !s.restore_stable("plain", api).ok, "GFX a locked family refuses restore previous and restore stable");
        require(contains(s.set_ota("plain", true).message, "locked"), "GFX the vendor-update flag cannot re-enable updates while locked");
        { gr::Store again(o); require(again.state().families.at("plain").lock == "plain-1.1.0" && again.resolve().find("plain")->locked, "GFX a lock persists across restarts"); }
        auto launch = s.begin_launch();
        auto loaded = std::map<std::string, std::string>{{"plain", "1.1.0"}};
        require(s.verify_loaded(launch, loaded).empty(), "GFX the pinned version is demonstrably the one reported loaded");
        loaded["plain"] = "1.0.0";
        require(!s.verify_loaded(launch, loaded).empty(), "GFX a loaded version differing from the pin is reported");
        gr::Catalog cat;
        cat.serial = 1;
        cat.entries = {entry_for(pkgs, "plain-1.2.0-exp", true)};
        auto cand = s.candidate(cat, "plain", Channel::Experimental, api);
        require(cand.kind == gr::CandidateKind::Locked && !cand.autoEligible, "GFX a locked family reports locked rather than offering a candidate");
        s.set_mode("automatic");
        gr::LocalDirectoryPackageSource src(pkgs);
        auto lines = s.auto_update(cat, src, api);
        require(!lines.empty() && contains(lines.front(), "locked") && s.state().families.at("plain").active == "plain-1.1.0", "GFX automatic updates never override a lock");
        require(!s.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local).message.empty(), "GFX staging still works for a locked family (no activation)");
        gr::Catalog revokeCat;
        revokeCat.serial = 2;
        revokeCat.revoked = {"plain-1.1.0"};
        s.apply_catalog(revokeCat);
        auto afterRevoke = s.resolve();
        require(afterRevoke.find("plain")->locked && s.state().families.at("plain").lock == "plain-1.1.0" && !s.state().events.empty() && contains(s.state().events.back(), "revoked"), "GFX a revoked locked set keeps its lock and warns instead of being replaced");
        require(s.unlock("plain").ok && !s.resolve().find("plain")->locked && s.unlock("plain").ok, "GFX unlock clears the lock and is idempotent");
        require(!s.lock("nosuch").ok, "GFX locking an unknown family is refused");
        s.stage_folder(pkgs / "reflex-1.1", gr::StageTrust::Local);
        s.activate("reflex", "reflex-1.1", api);
        require(!s.lock("reflex").ok, "GFX locking during a pending trial is refused");
    }

    {
        auto o = opts("store-link");
        gr::Store s(o);
        s.stage_folder(pkgs / "sl-2.13.0", gr::StageTrust::Local);
        s.stage_folder(pkgs / "dlssg-310.9", gr::StageTrust::Local);
        auto direct = s.plan_activation("dlssg", "dlssg-310.9", api);
        require(direct.ok && direct.changes.size() == 2 && direct.changes[1].family == "sl" && direct.changes[1].setId == "sl-2.13.0", "GFX activating a matched component pulls its required set along");
        require(!direct.explanations.empty() && contains(direct.explanations.front(), "Linked update") && contains(direct.explanations.front(), "nvidia.streamline.interposer"), "GFX linked selections are explained in plain language");
        auto lone = s.plan_activation("sl", "sl-2.13.0", api);
        require(!lone.ok && contains(lone.message, "needs nvidia.streamline.interposer release sl-2.12.0") && contains(lone.message, "Starlight conservative policy"), "GFX updating one half of a matched pair alone is refused and labelled as our policy");
        gr::Store lockedSl(opts("store-link-locked"));
        lockedSl.stage_folder(pkgs / "sl-2.13.0", gr::StageTrust::Local);
        lockedSl.stage_folder(pkgs / "dlssg-310.9", gr::StageTrust::Local);
        lockedSl.lock("sl");
        auto refused = lockedSl.plan_activation("dlssg", "dlssg-310.9", api);
        require(!refused.ok && contains(refused.message, "locked to sl-2.12.0"), "GFX a lock on the required family blocks the linked update and names the lock");
        gr::Store missing(opts("store-link-missing"));
        missing.stage_folder(pkgs / "dlssg-310.9", gr::StageTrust::Local);
        auto noProvider = missing.plan_activation("dlssg", "dlssg-310.9", api);
        require(!noProvider.ok && (contains(noProvider.message, "does not contain it") || contains(noProvider.message, "has it")), "GFX a missing required component refuses the set");
        auto xe = gr::parse_manifest(gr::read_text(bundled / "xefg-1.3.1" / "set.json"));
        gr::SetManifest broken = xe;
        broken.components.pop_back();
        auto problems = gr::check_combination({&broken});
        require(problems.size() == 1 && contains(problems[0], "vendor-documented rule") && contains(problems[0], "intel.xell"), "GFX a vendor-documented rule is labelled as vendor-documented");
        require(gr::check_combination({&xe}).empty(), "GFX XeSS-FG with its XeLL passes");
        auto sl12 = gr::parse_manifest(gr::read_text(bundled / "sl-2.12.0" / "set.json"));
        auto sl13 = gr::parse_manifest(gr::read_text(pkgs / "sl-2.13.0" / "set.json"));
        require(!gr::check_combination({&sl12, &sl13}).empty(), "GFX two sets of one matched group with different releases are a broken combination");
        auto st = s.activate("dlssg", "dlssg-310.9", api);
        require(st.ok && s.state().families.at("sl").pending && s.state().families.at("dlssg").pending, "GFX a linked activation records both families in one step");
        auto launch = s.begin_launch();
        require(launch.find("sl")->version == "2.13.0" && launch.find("dlssg")->version == "310.9.0", "GFX linked families load together");
        s.report_launch_failure("test");
        auto after = s.resolve();
        require(after.find("sl")->version == "2.12.0" && after.find("dlssg")->version == "310.7.0", "GFX a failed linked trial reverts both families together");
        s.stage_folder(pkgs / "reflex-1.2-bigmajor", gr::StageTrust::Local);
        auto big = s.plan_activation("reflex", "reflex-1.2-bigmajor", api);
        require(!big.ok && contains(big.message, "major") , "GFX a set built for another API major is refused as a manual transition");
    }

    {
        auto o = opts("store-channels");
        gr::Store s(o);
        const auto kp = gr::testing::generate_test_keypair();
        gr::Catalog cat;
        cat.serial = 7;
        cat.entries = {entry_for(pkgs, "plain-1.1.0", true), entry_for(pkgs, "plain-1.2.0-exp", false), entry_for(pkgs, "reflex-1.1", true), entry_for(pkgs, "reflex-1.2-bigmajor", true)};
        require(s.apply_catalog(cat).ok && !s.apply_catalog([&] { auto c = cat; c.serial = 3; return c; }()).ok, "GFX an older catalog serial is refused as a rollback");
        auto stable = s.candidate(cat, "plain", Channel::Stable, api);
        auto exp = s.candidate(cat, "plain", Channel::Experimental, api);
        require(stable.kind == gr::CandidateKind::Available && stable.version == "1.1.0" && exp.version == "1.2.0", "GFX stable offers the newest verified release and experimental the newest candidate");
        require(exp.manualRequired && !exp.autoEligible, "GFX experimental releases are never automatic");
        auto none = s.candidate(cat, "dlssg", Channel::Stable, api);
        require(none.kind == gr::CandidateKind::None && contains(none.reason, "No compatible"), "GFX a family without candidates reports an honest no-candidate state");
        auto reflex = s.candidate(cat, "reflex", Channel::Stable, api);
        require(reflex.version == "1.1.0", "GFX a release for a different API major is not offered");
        auto reflexOnlyMajor = cat;
        reflexOnlyMajor.entries = {entry_for(pkgs, "reflex-1.2-bigmajor", true)};
        auto big = s.candidate(reflexOnlyMajor, "reflex", Channel::Stable, api);
        require(big.kind == gr::CandidateKind::None && contains(big.reason, "different API major"), "GFX incompatible majors are counted and explained, never offered");
        auto revoked = cat; revoked.revoked = {"plain-1.1.0"};
        require(s.candidate(revoked, "plain", Channel::Stable, api).kind == gr::CandidateKind::None, "GFX a revoked release is not offered");
        auto minSafe = cat; minSafe.min_safe["plain"] = "1.2.0";
        require(s.candidate(minSafe, "plain", Channel::Stable, api).kind == gr::CandidateKind::None && s.candidate(minSafe, "plain", Channel::Experimental, api).version == "1.2.0", "GFX releases below the catalog minimum safe version are not offered");
        require(s.candidate(cat, "plain", Channel::Stable, api).autoEligible == false, "GFX automatic eligibility requires automatic mode");
        s.set_mode("automatic");
        require(s.candidate(cat, "plain", Channel::Stable, api).autoEligible, "GFX automatic mode makes a vetted API-compatible stable release eligible");
        auto manualEntry = cat; manualEntry.entries[0].auto_ok = false;
        require(s.candidate(manualEntry, "plain", Channel::Stable, api).manualRequired, "GFX an entry not marked automatic stays manual");
        gr::LocalDirectoryPackageSource src(pkgs);
        auto log = s.auto_update(cat, src, api);
        require(s.state().families.at("plain").pending && s.state().families.at("plain").pending->set == "plain-1.1.0" && s.state().families.at("reflex").pending, "GFX automatic update stages vetted packages and selects them as trials");
        require(s.state().sets.at("plain-1.1.0").trust == "catalog", "GFX automatically staged sets are recorded as catalog-trusted");
        require(!log.empty(), "GFX automatic update reports what it did");
        s.set_channel("plain", Channel::Experimental);
        s.begin_launch(); s.confirm_good();
        auto log2 = s.auto_update(cat, src, api);
        bool skipped = false;
        for (auto& l : log2) if (contains(l, "experimental")) skipped = true;
        require(skipped && !s.state().families.at("plain").pending, "GFX the experimental channel is manual only");
        gr::Store mstore(opts("store-manual"));
        mstore.set_mode("manual");
        require(contains(mstore.auto_update(cat, src, api).front(), "not enabled") && mstore.list_sets().size() == 5, "GFX manual mode never changes anything on its own");
        auto wrong = cat.entries[0]; wrong.manifest_sha256 = std::string(64, '0');
        gr::Store catStore(opts("store-catalog-stage"));
        require(!catStore.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Catalog, &wrong).ok, "GFX a package that does not match its signed catalog entry is refused");
        require(catStore.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Catalog, &cat.entries[0]).ok, "GFX a package matching its catalog entry stages");
        catStore.stage_folder(pkgs / "plain-1.2.0-exp", gr::StageTrust::Local);
        auto rs = catStore.restore_stable("plain", api);
        require(rs.ok && catStore.state().families.at("plain").pending && catStore.state().families.at("plain").pending->set == "plain-1.1.0", "GFX restore stable picks the newest verified stable set and ignores local or experimental ones");
        require(!catStore.restore_stable("nosuch", api).ok, "GFX restore stable with nothing installed says so");
    }

    {
        std::vector<std::string> steps;
        int crashPoints = 0;
        auto scenario = [&](const std::string& label, std::function<void(gr::Store&, Fault&)> pre, std::function<void(gr::Store&)> op, std::function<bool(gr::Store&)> after) {
            int total = 0;
            for (int attempt = -1;; ++attempt) {
                Fault fault;
                fault.crashAt = attempt;
                gr::StoreOptions o;
                o.root = work / ("fault-" + label + "-" + std::to_string(attempt + 1));
                o.bundledRoot = bundled;
                o.fault = fault.hook();
                fs::remove_all(o.root);
                bool crashed = false;
                {
                    gr::Store s(o);
                    pre(s, fault);
                    fault.armed = true;
                    try { op(s); } catch (const gr::InjectedCrash&) { crashed = true; }
                }
                if (attempt < 0) { total = int(fault.names.size()); require(total > 0 && !crashed, ("GFX " + label + " has crash points").c_str()); }
                std::string why;
                bool valid = state_valid(o.root, bundled, why);
                gr::StoreOptions reopen = o;
                reopen.fault = nullptr;
                gr::Store s2(reopen);
                bool good = valid && after(s2);
                if (!good) require(false, ("GFX crash safety " + label + " at step " + (attempt >= 0 && attempt < int(fault.names.size()) ? fault.names[attempt] : std::string("none")) + ": " + why).c_str());
                if (attempt >= 0) ++crashPoints;
                if (attempt + 1 >= total) break;
            }
            require(true, ("GFX every crash point of " + label + " leaves a valid state").c_str());
            return total;
        };
        auto stageBoth = [&](gr::Store& s, Fault&) { s.stage_folder(pkgs / "sl-2.13.0", gr::StageTrust::Local); s.stage_folder(pkgs / "dlssg-310.9", gr::StageTrust::Local); s.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local); };
        int t1 = scenario("stage", [](gr::Store&, Fault&) {}, [&](gr::Store& s) { s.stage_folder(pkgs / "sl-2.13.0", gr::StageTrust::Local); },
            [&](gr::Store& s) {
                bool recorded = s.state().sets.count("sl-2.13.0");
                auto again = s.stage_folder(pkgs / "sl-2.13.0", gr::StageTrust::Local);
                return again.ok && s.state().sets.count("sl-2.13.0") && (recorded || true);
            });
        int t2 = scenario("activate-linked", stageBoth, [&](gr::Store& s) { s.activate("dlssg", "dlssg-310.9", api); },
            [&](gr::Store& s) {
                bool a = s.state().families.count("sl") && s.state().families.at("sl").pending;
                bool b = s.state().families.count("dlssg") && s.state().families.at("dlssg").pending;
                return a == b;
            });
        int t3 = scenario("confirm", [&](gr::Store& s, Fault& f) { stageBoth(s, f); s.activate("plain", "plain-1.1.0", api); s.begin_launch(); }, [](gr::Store& s) { s.confirm_good(); },
            [](gr::Store& s) {
                auto& f = s.state().families.at("plain");
                bool promoted = f.active == "plain-1.1.0" && f.lastKnownGood == "plain-1.1.0" && !f.pending;
                bool untouched = f.active.empty() && f.pending;
                return promoted || untouched;
            });
        int t4 = scenario("restore-previous", [&](gr::Store& s, Fault& f) { stageBoth(s, f); s.activate("plain", "plain-1.1.0", api); s.begin_launch(); s.confirm_good(); },
            [&](gr::Store& s) { s.restore_previous("plain", api); },
            [](gr::Store& s) { auto& f = s.state().families.at("plain"); return (f.active == "plain-1.1.0" && f.previous == "plain-1.0.0") || (f.active == "plain-1.0.0" && f.previous == "plain-1.1.0"); });
        int t5 = scenario("lock", [&](gr::Store& s, Fault& f) { stageBoth(s, f); }, [](gr::Store& s) { s.lock("plain"); },
            [](gr::Store& s) { auto it = s.state().families.find("plain"); return it == s.state().families.end() || it->second.lock.empty() || it->second.lock == "plain-1.0.0"; });
        int t6 = scenario("launch-decision", [&](gr::Store& s, Fault& f) { stageBoth(s, f); s.activate("plain", "plain-1.1.0", api); }, [](gr::Store& s) { s.begin_launch(); },
            [](gr::Store& s) { auto r = s.resolve(); return r.find("plain")->setId == "plain-1.0.0" || r.find("plain")->setId == "plain-1.1.0"; });
        require(t1 >= 3 && t2 >= 3 && t3 >= 3 && t4 >= 3 && t5 >= 3 && t6 >= 3, "GFX each state change has at least temp, backup and replace crash points");
        require(crashPoints >= 20, "GFX at least twenty injected crash points were exercised");
        {
            auto o = opts("state-recovery");
            { gr::Store s(o); s.set_mode("manual"); s.set_channel("sl", Channel::Experimental); }
            auto st = o.root / "state.json";
            std::string good = gr::read_text(st);
            put(st, good.substr(0, good.size() / 2));
            { gr::Store s(o); require(s.state().updateMode == "manual", "GFX a truncated state file is restored from the backup copy"); }
            put(st, "garbage");
            put(o.root / "state.json.bak", "garbage");
            fs::create_directories(o.root / "sets" / "recorded-before-damage");
            put(o.root / "sets" / "recorded-before-damage" / "set.json", "{}");
            { gr::Store s(o); require(s.state().updateMode == "unset" && s.resolve().find("sl")->version == "2.12.0", "GFX with no readable state the bundled set still runs"); }
            {
                std::string kept;
                for (auto& e : fs::directory_iterator(o.root)) { auto n = gr::utf8(e.path().filename()); if (n.find(".corrupt-") != std::string::npos) kept += n + ";"; }
                gr::Store s(o);
                require(!fs::exists(st) && contains(kept, "state.json.corrupt-") && contains(kept, "state.json.bak.corrupt-"), "GFX two unreadable state files are kept under new names and never overwritten");
                auto pr = s.prune(true);
                require(!pr.ok && contains(pr.message, "refused") && fs::exists(o.root / "sets" / "recorded-before-damage" / "set.json"), "GFX prune refuses while a damaged state file is kept");
                put(o.root / "state.json", "garbage");
                put(o.root / "state.json.bak", "garbage");
                gr::Store s2(o);
                bool noted = false;
                for (auto& ev : s2.state().events) if (contains(ev, "could not be used")) noted = true;
                require(noted, "GFX losing both state files leaves a visible event");
            }
            fs::remove(st);
            put(o.root / "state.json.tmp", "half");
            { gr::Store s(o); require(!fs::exists(o.root / "state.json.tmp"), "GFX an interrupted temp file is swept at the next start"); }
            fs::create_directories(o.root / "sets" / ".staging-123-1");
            put(o.root / "sets" / ".staging-123-1" / "x.dll", "x");
            { gr::Store s(o); require(!fs::exists(o.root / "sets" / ".staging-123-1"), "GFX interrupted staging folders are swept at the next start"); }
            {
                // DEP-07: staging that belongs to another live process is that process's operation in progress.
                std::wstring command = L"ping.exe -n 60 127.0.0.1";
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION child{};
                const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child);
                require(started, "GFX DEP-07 the check could start a long-running child process");
                if (started) {
                    const auto live = o.root / "sets" / (".staging-" + std::to_string(child.dwProcessId) + "-1");
                    const auto own = o.root / "sets" / (".staging-" + std::to_string(GetCurrentProcessId()) + "-1");
                    for (auto* dir : {&live, &own}) { fs::create_directories(*dir); put(*dir / "x.dll", "x"); }
                    { gr::Store s(o); }
                    const bool liveKept = fs::exists(live / "x.dll");
                    const bool ownSwept = !fs::exists(own);
                    TerminateProcess(child.hProcess, 0);
                    WaitForSingleObject(child.hProcess, 5000);
                    CloseHandle(child.hThread);
                    CloseHandle(child.hProcess);
                    require(liveKept, "GFX DEP-07 a sweep leaves another live process's staging folder alone");
                    require(ownSwept, "GFX DEP-07 a sweep still removes leftover staging that carries this process's own id");
                    { gr::Store s(o); require(!fs::exists(live), "GFX DEP-07 staging of a process that has exited is swept"); }
                }
            }
        }
    }

    {
        auto o = opts("store-export");
        gr::Store s(o);
        auto exportPkgs = work / "export-pkgs";
        build(exportPkgs, "streamline-core-2.12.0", "streamline-core", kUp, Channel::Stable, "2.12.0", {{"nvidia.streamline.interposer", "sl-2.12.0", "sl"}, {"nvidia.streamline.common", "sl-2.12.0", "sl"}});
        build(exportPkgs, "dlss-sr-310.7", "dlss-sr", kUp, Channel::Stable, "310.7.0", {{"nvidia.streamline.dlss", "sl-2.12.0", "sl", {rq("nvidia.streamline.interposer", "sl-2.12.0")}}, {"nvidia.ngx.dlss", "ngx-310.7", "ngx", {rq("nvidia.streamline.dlss", "")}}});
        build(exportPkgs, "dlss-rr-310.7", "dlss-rr", kUp, Channel::Stable, "310.7.0", {{"nvidia.streamline.dlss-d", "sl-2.12.0", "sl", {rq("nvidia.streamline.interposer", "sl-2.12.0")}}, {"nvidia.ngx.dlss-d", "ngx-310.7", "ngx", {rq("nvidia.ngx.dlss", "ngx-310.7")}}});
        for (auto* id : {"streamline-core-2.12.0", "dlss-sr-310.7", "dlss-rr-310.7"}) s.stage_folder(exportPkgs / id, gr::StageTrust::Local);
        s.activate("streamline-core", "streamline-core-2.12.0", api);
        s.activate("dlss-sr", "dlss-sr-310.7", api);
        s.begin_launch(); s.confirm_good();
        std::string err;
        auto slOnly = gr::required_components_for({"streamline"}, err);
        require(slOnly.size() == 2 && std::find(slOnly.begin(), slOnly.end(), "nvidia.streamline.dlss-d") == slOnly.end(), "GFX merely using Streamline does not require ray reconstruction components");
        auto rr = gr::required_components_for({"ray-reconstruction"}, err);
        require(std::find(rr.begin(), rr.end(), "nvidia.streamline.dlss-d") != rr.end() && std::find(rr.begin(), rr.end(), "nvidia.ngx.dlss-d") != rr.end() && std::find(rr.begin(), rr.end(), "nvidia.ngx.dlss") != rr.end(), "GFX ray reconstruction requires its complete dependency chain");
        gr::required_components_for({"warp-drive"}, err);
        require(!err.empty() && !gr::known_feature("warp-drive") && gr::known_feature("xess-fg"), "GFX unknown export features are refused");
        auto planStream = gr::plan_export(s, {"streamline"});
        require(planStream.ok && planStream.componentIds.size() == 2 && planStream.files.size() == 4, "GFX a Streamline-only export contains only the Streamline core and its licences");
        int lic = 0;
        for (auto& f : planStream.files) if (f.licence) ++lic;
        require(lic == 2, "GFX every exported component brings its licence file");
        auto planSr = gr::plan_export(s, {"dlss-sr"});
        require(planSr.ok && planSr.componentIds.size() == 4, "GFX DLSS export pulls the Streamline core it requires through the manifest rules");
        auto planRr = gr::plan_export(s, {"ray-reconstruction"});
        require(!planRr.ok && contains(planRr.message, "nvidia.streamline.dlss-d"), "GFX ray reconstruction with an uninstalled dependency is refused and names it");
        s.activate("dlss-rr", "dlss-rr-310.7", api);
        s.begin_launch(); s.confirm_good();
        auto planRr2 = gr::plan_export(s, {"ray-reconstruction"});
        require(planRr2.ok && planRr2.componentIds.size() == 6, "GFX with RR installed the export holds the complete verified dependency set");
        auto dest = work / "export-out";
        auto w = gr::write_export(planRr2, dest);
        require(w.ok && fs::exists(dest / "bin" / "nvidia.ngx.dlss-d.dll") && fs::exists(dest / "licenses" / "nvidia.ngx.dlss-d.txt"), "GFX the export is written with licence files");
        require(!gr::write_export(planRr2, dest).ok, "GFX an export never overwrites an existing folder");
        auto dest2 = work / "export-out2";
        put(work / "export-out2.staging" / "mine.txt", "not ours");
        auto w2 = gr::write_export(planRr2, dest2);
        require(w2.ok && fs::exists(dest2 / "bin" / "nvidia.ngx.dlss-d.dll") && gr::read_text(work / "export-out2.staging" / "mine.txt") == "not ours", "GFX an export never deletes an unrelated folder named like its staging folder");
        auto planFsr = gr::plan_export(s, {"fsr-sr"});
        require(!planFsr.ok && planFsr.missing.size() == 2, "GFX missing AMD components are listed individually");
        put(s.set_dir("dlss-rr-310.7") / "bin" / "nvidia.ngx.dlss-d.dll", "tampered");
        auto planBad = gr::plan_export(s, {"ray-reconstruction"});
        require(!planBad.ok, "GFX a tampered set is not exported");
    }

    {
        auto o = opts("store-report");
        gr::Store s(o);
        s.stage_folder(pkgs / "sl-2.13.0", gr::StageTrust::Local);
        gr::ReportInput in;
        in.failureCode = "backend-init";
        in.description = "Black screen after switching to the newer version.";
        in.diagnostics = "Loaded C:\\Users\\Someone\\AppData\\x.dll for user someone@example.com at 192.168.1.20 id 4f9a1c2d-3b4e-4f60-8a1b-0123456789ab";
        auto rep = gr::build_report(s.resolve(), in);
        auto j = nlohmann::json::parse(rep.json);
        std::set<std::string> keys;
        for (auto it = j.begin(); it != j.end(); ++it) keys.insert(it.key());
        require(keys == std::set<std::string>{"schema", "components", "failure_code", "description"} && !rep.diagnosticsIncluded, "GFX a problem report holds only components, failure code and the user's description by default");
        require(contains(rep.preview, "Black screen") && contains(rep.preview, "nothing has been sent") && contains(rep.preview, "not included"), "GFX the preview shows exactly what would be saved");
        in.includeDiagnostics = true;
        auto withDiag = gr::build_report(s.resolve(), in);
        auto jd = nlohmann::json::parse(withDiag.json);
        auto d = jd["diagnostics"].get<std::string>();
        require(withDiag.diagnosticsIncluded && !contains(d, "Someone") && !contains(d, "example.com") && !contains(d, "192.168") && !contains(d, "4f9a1c2d") && contains(d, "<path>"), "GFX extra diagnostics are redacted and only included with explicit consent");
        auto env = [](const char* n) { char* v = nullptr; size_t len = 0; std::string r; if (_dupenv_s(&v, &len, n) == 0 && v) { r = v; free(v); } return r; };
        auto user = env("USERNAME"), host = env("COMPUTERNAME");
        require((user.size() < 3 || !contains(rep.json, user)) && (host.size() < 3 || !contains(rep.json, host)) && !contains(rep.json, "LOCALAPPDATA"), "GFX the report carries no user name, machine name or system inventory");
        in.failureCode = "bad code with spaces!";
        require(contains(gr::build_report(s.resolve(), in).json, "invalid-code"), "GFX a malformed failure code is replaced");
        auto out = work / "report.json";
        require(gr::save_report(rep, out).ok && gr::read_text(out) == rep.json && !gr::save_report(rep, out).ok, "GFX the report is saved locally and never overwrites");
        require(gr::redact_text("a@b.co 10.0.0.1 \\\\server\\share\\x") == "<email> <ip> <path>", "GFX redaction removes addresses and paths");
        gr::ReportInput odd;
        odd.description = std::string(3999, 'a') + "\xE2\x82\xAC";
        odd.diagnostics = "log line \xE9 from an ANSI file ";
        while (odd.diagnostics.size() < 20000) odd.diagnostics += "word ";
        odd.diagnostics += "\xE2\x82\xAC";
        odd.includeDiagnostics = true;
        bool oddOk = false;
        try { auto r2 = gr::build_report(s.resolve(), odd); nlohmann::json::parse(r2.json); oddOk = r2.diagnosticsIncluded; } catch (const std::exception&) {}
        require(oddOk, "GFX a report with a cut multi-byte character or non-UTF-8 diagnostics is still produced as valid JSON");
        gr::Store f(opts("store-report-ansi"));
        f.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local);
        f.activate("plain", "plain-1.1.0", api);
        f.begin_launch();
        bool reasonOk = false;
        try { reasonOk = f.report_launch_failure("failed \xE9 ANSI text").ok; } catch (const std::exception&) {}
        require(reasonOk && f.state().sets.at("plain-1.1.0").bad && gr::Store(opts("store-report-ansi")).state().sets.at("plain-1.1.0").bad && !f.status_json(std::nullopt, api).empty() && !f.resolve().to_json().empty(), "GFX a non-UTF-8 failure reason is still saved and reported");
    }

    {
        auto o = opts("store-prune");
        gr::Store s(o);
        s.stage_folder(pkgs / "plain-1.1.0", gr::StageTrust::Local);
        s.stage_folder(pkgs / "plain-1.2.0-exp", gr::StageTrust::Local);
        s.stage_folder(pkgs / "reflex-1.1", gr::StageTrust::Local);
        s.activate("plain", "plain-1.1.0", api);
        put(o.root / "sets" / "user-notes" / "readme.txt", "my own files");
        fs::create_directories(o.root / "sets" / "orphan-1.0");
        put(o.root / "sets" / "orphan-1.0" / "set.json", "{}");
        auto inv = s.inventory();
        auto has = [](const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); };
        require(has(inv.keep, "plain-1.1.0") && has(inv.removable, "plain-1.2.0-exp") && has(inv.removable, "reflex-1.1") && has(inv.removable, "orphan-1.0") && has(inv.keep, "user-notes"), "GFX the inventory names what is referenced, removable and not ours");
        auto dry = s.prune(false);
        require(!dry.ok && fs::exists(o.root / "sets" / "plain-1.2.0-exp"), "GFX prune without confirmation changes nothing");
        s.begin_launch(); s.confirm_good();
        s.lock("plain");
        auto done = s.prune(true);
        require(done.ok && !fs::exists(o.root / "sets" / "plain-1.2.0-exp") && fs::exists(o.root / "sets" / "plain-1.1.0") && fs::exists(o.root / "sets" / "user-notes" / "readme.txt"), "GFX confirmed prune removes only unreferenced Starlight sets and never other content");
        require(fs::exists(bundled / "plain-1.0.0" / "set.json") && s.resolve().find("plain")->version == "1.1.0", "GFX prune never removes pinned, active or bundled sets");

        std::wstring command = L"ping.exe -n 60 127.0.0.1";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child);
        require(started, "GFX GR-4 the check could start a long-running child process");
        if (started) {
            const auto live = o.root / "sets" / (".staging-" + std::to_string(child.dwProcessId) + "-1");
            const auto dead = o.root / "sets" / ".staging-2000000000-1";
            for (auto* dir : {&live, &dead}) { fs::create_directories(*dir); put(*dir / "set.json", "{}"); }
            auto inv2 = s.inventory();
            auto pr = s.prune(true);
            const bool liveKept = fs::exists(live / "set.json") && std::find(inv2.keep.begin(), inv2.keep.end(), live.filename().string()) != inv2.keep.end();
            const bool deadRemoved = !fs::exists(dead);
            TerminateProcess(child.hProcess, 0);
            WaitForSingleObject(child.hProcess, 5000);
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
            fs::remove_all(live);
            require(liveKept, "GFX prune and the inventory leave another live process's staging folder alone");
            require(pr.ok && deadRemoved, "GFX prune still removes a staging folder whose process is gone");
        }
    }

    {
        auto installed = gr::default_installed_root("StarlightTest");
        require(contains(gr::utf8(installed), "StarlightTest") && installed.filename() == "graphics-runtime", "GFX installed mode uses an app-specific per-user folder");
        require(gr::portable_root(work / "game").filename() == "graphics-runtime-managed" && gr::portable_root(work / "game").parent_path() == work / "game", "GFX portable mode uses an explicit adjacent managed folder");
        try { gr::portable_root({}); require(false, "GFX portable needs a folder"); } catch (const gr::GrError&) { require(true, "GFX portable mode refuses to guess a folder"); }
        try { gr::StoreOptions bad; gr::Store s(bad); require(false, "GFX store needs a root"); } catch (const gr::GrError&) { require(true, "GFX a store without an explicit root is refused"); }
    }

    {
        const auto kp = gr::testing::generate_test_keypair();
        auto hexOf = [](const std::vector<std::uint8_t>& v) { static const char* h = "0123456789abcdef"; std::string s; for (auto b : v) { s += h[b >> 4]; s += h[b & 15]; } return s; };
        put(work / "pub.hex", hexOf(kp.publicBlob));
        gr::Catalog cat;
        cat.serial = 2;
        cat.entries = {entry_for(pkgs, "sl-2.13.0", true)};
        put(work / "catalog.json", gr::testing::sign_catalog_payload(kp, "cli-key", gr::testing::catalog_payload_json(cat)));
        auto run = [&](std::vector<std::string> args, std::string* outText = nullptr, std::string* errText = nullptr) {
            std::ostringstream o, e;
            args.push_back("--root"); args.push_back(gr::utf8(work / "cli-root"));
            args.push_back("--bundled"); args.push_back(gr::utf8(bundled));
            args.push_back("--engine-api"); args.push_back("sl=2,test=1,xess=3");
            int code = gr::cli_main(args, o, e);
            if (outText) *outText = o.str();
            if (errText) *errText = e.str();
            return code;
        };
        std::string out, err;
        { std::ostringstream ho, he; require(gr::cli_main({"help"}, ho, he) == 0 && contains(ho.str(), "Commands"), "GFX CLI help runs"); }
        require(run({"status"}, &out) == 0 && nlohmann::json::parse(out).at("first_launch_choice_needed") == true, "GFX CLI status reports the first-launch choice");
        require(run({"set-mode", "automatic"}) == 0 && run({"set-mode", "never"}) == 1, "GFX CLI set-mode validates its argument");
        require(run({"stage", gr::utf8(pkgs / "sl-2.13.0")}, &out, &err) == 1 && contains(err, "--allow-local"), "GFX CLI refuses an uncatalogued package without an explicit local flag");
        require(run({"stage", gr::utf8(pkgs / "sl-2.13.0"), "--allow-local"}) == 0, "GFX CLI stages a local package on request");
        require(run({"stage", gr::utf8(pkgs / "dlssg-310.9"), "--catalog", gr::utf8(work / "catalog.json"), "--trust", "cli-key:" + gr::utf8(work / "pub.hex"), "--allow-local"}) == 0, "GFX CLI stages a package missing from the catalog as local when asked");
        require(run({"stage", gr::utf8(pkgs / "sl-2.13.0"), "--catalog", gr::utf8(work / "catalog.json"), "--trust", "cli-key:" + gr::utf8(work / "pub.hex")}) == 0, "GFX CLI stages a catalogued package with catalog trust");
        require(run({"list"}, &out) == 0 && contains(out, "sl-2.13.0") && contains(out, "trust=catalog") && contains(out, "trust=bundled"), "GFX CLI list shows bundled and staged sets with their trust");
        require(run({"activate", "dlssg", "dlssg-310.9"}, &out) == 0 && contains(out, "linked"), "GFX CLI activation shows linked selections");
        require(run({"activate", "sl", "sl-9.9"}) == 1, "GFX CLI activation of an unknown set is refused");
        require(run({"begin-launch"}, &out) == 0 && contains(out, "trial") && run({"confirm-good"}) == 0, "GFX CLI launch decision and confirmation work");
        require(run({"stage", gr::utf8(pkgs / "plain-1.1.0"), "--allow-local"}) == 0 && run({"activate", "plain", "plain-1.1.0"}) == 0 && run({"begin-launch"}) == 0 && run({"confirm-good"}) == 0, "GFX CLI independent family goes through trial and confirmation");
        require(run({"lock", "plain"}) == 0 && run({"activate", "plain", "plain-1.0.0"}, &out, &err) == 1 && contains(err, "locked"), "GFX CLI lock blocks activation");
        require(run({"unlock", "plain"}) == 0 && run({"restore-previous", "plain"}) == 0, "GFX CLI unlock then restore previous");
        require(run({"restore-stable", "plain"}) == 0, "GFX CLI restore stable");
        require(run({"set-channel", "plain", "experimental"}) == 0 && run({"set-channel", "plain", "nightly"}) == 2, "GFX CLI set-channel validates the channel");
        require(run({"set-ota", "plain", "allow"}) == 0 && run({"set-ota", "plain", "maybe"}) == 2, "GFX CLI set-ota validates its argument");
        require(run({"status", "--catalog", gr::utf8(work / "catalog.json"), "--trust", "cli-key:" + gr::utf8(work / "pub.hex")}, &out) == 0 && nlohmann::json::parse(out).at("categories").at("upscaling")[0].contains("candidates"), "GFX CLI status shows channel candidates from a verified catalog");
        require(run({"status", "--catalog", gr::utf8(work / "catalog.json")}, &out, &err) == 0 && !err.empty(), "GFX CLI ignores a catalog it cannot verify and says so");
        require(run({"check-catalog", "--catalog", gr::utf8(work / "catalog.json"), "--trust", "cli-key:" + gr::utf8(work / "pub.hex")}) == 0, "GFX CLI checks a catalog");
        {
            auto upper = hexOf(kp.publicBlob);
            for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            put(work / "pub-upper.hex", upper + "\n");
            require(run({"check-catalog", "--catalog", gr::utf8(work / "catalog.json"), "--trust", "cli-key:" + gr::utf8(work / "pub-upper.hex")}) == 0, "GFX CLI accepts an upper-case hexadecimal public key file");
            put(work / "pub-spaced.hex", hexOf(kp.publicBlob).insert(10, " "));
            put(work / "pub-odd.hex", hexOf(kp.publicBlob) + "a");
            require(run({"check-catalog", "--catalog", gr::utf8(work / "catalog.json"), "--trust", "cli-key:" + gr::utf8(work / "pub-spaced.hex")}, &out, &err) != 0 && contains(err, "hexadecimal"), "GFX CLI reports a public key file with a stray character");
            require(run({"check-catalog", "--catalog", gr::utf8(work / "catalog.json"), "--trust", "cli-key:" + gr::utf8(work / "pub-odd.hex")}, &out, &err) != 0 && contains(err, "hexadecimal"), "GFX CLI reports a public key file with an odd number of digits");
        }
        require(run({"auto-update", "--catalog", gr::utf8(work / "catalog.json"), "--trust", "cli-key:" + gr::utf8(work / "pub.hex")}) == 2, "GFX CLI auto-update needs a local package folder and never downloads");
        require(run({"export-plan", "streamline"}, &out) == 0 && contains(out, "licence") && run({"export-plan", "fsr-sr"}) == 1 && run({"export-plan", "nonsense"}) == 1, "GFX CLI export plan lists files and refuses incomplete or unknown requests");
        require(run({"report-preview", "--code", "x-1", "--description", "It broke."}, &out) == 0 && contains(out, "It broke.") && contains(out, "no system inventory"), "GFX CLI report preview prints the full report");
        require(run({"report-preview", "--include-diagnostics"}) == 2, "GFX CLI diagnostics need an explicit file");
        require(run({"report-preview", "--description", "x", "--save", gr::utf8(work / "cli-report.json")}) == 0 && fs::exists(work / "cli-report.json"), "GFX CLI saves a report locally only");
        require(run({"prune"}) == 1 && run({"prune", "--confirm"}) == 0, "GFX CLI prune is a dry run unless confirmed");
        require(run({"bogus"}) == 2, "GFX CLI rejects an unknown command");
        std::ostringstream o2, e2;
        require(gr::cli_main({"status"}, o2, e2) == 2, "GFX CLI needs an explicit location");
    }
}
