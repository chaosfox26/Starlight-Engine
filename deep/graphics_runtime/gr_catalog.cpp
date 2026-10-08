#include "graphics_runtime.hpp"
#include "../src/json.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <cctype>

namespace starlight::gfxrt {
using json = nlohmann::json;

namespace {
std::vector<std::uint8_t> from_hex(const std::string& s) {
    if (s.size() % 2) throw GrError("Odd-length hex text");
    std::vector<std::uint8_t> out;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw GrError("Bad hex text");
    };
    for (std::size_t i = 0; i < s.size(); i += 2) out.push_back(static_cast<std::uint8_t>(nib(s[i]) * 16 + nib(s[i + 1])));
    return out;
}
std::string to_hex(const std::vector<std::uint8_t>& v) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (auto b : v) { s += h[b >> 4]; s += h[b & 15]; }
    return s;
}
struct Alg {
    BCRYPT_ALG_HANDLE h = nullptr;
    Alg() { if (BCryptOpenAlgorithmProvider(&h, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) throw GrError("ECDSA provider unavailable"); }
    ~Alg() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
};
struct Key {
    BCRYPT_KEY_HANDLE h = nullptr;
    ~Key() { if (h) BCryptDestroyKey(h); }
};
void check_keys(const json& j, std::initializer_list<const char*> allowed, const char* where) {
    if (!j.is_object()) throw GrError(std::string(where) + " must be an object");
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool ok = false;
        for (auto a : allowed) if (it.key() == a) ok = true;
        if (!ok) throw GrError("Unknown key '" + it.key() + "' in " + where);
    }
}
}

namespace testing {

KeyPair generate_test_keypair() {
    Alg alg;
    Key key;
    if (BCryptGenerateKeyPair(alg.h, &key.h, 256, 0) < 0 || BCryptFinalizeKeyPair(key.h, 0) < 0) throw GrError("Test key generation failed");
    KeyPair kp;
    auto exportBlob = [&](const wchar_t* type, std::vector<std::uint8_t>& out) {
        ULONG n = 0;
        if (BCryptExportKey(key.h, nullptr, type, nullptr, 0, &n, 0) < 0) throw GrError("Key export failed");
        out.resize(n);
        if (BCryptExportKey(key.h, nullptr, type, out.data(), n, &n, 0) < 0) throw GrError("Key export failed");
    };
    exportBlob(BCRYPT_ECCPUBLIC_BLOB, kp.publicBlob);
    exportBlob(BCRYPT_ECCPRIVATE_BLOB, kp.privateBlob);
    return kp;
}

std::string sign_catalog_payload(const KeyPair& kp, std::string_view keyId, std::string_view payload) {
    Alg alg;
    Key key;
    if (BCryptImportKeyPair(alg.h, nullptr, BCRYPT_ECCPRIVATE_BLOB, &key.h, const_cast<PUCHAR>(kp.privateBlob.data()), static_cast<ULONG>(kp.privateBlob.size()), 0) < 0) throw GrError("Private key import failed");
    auto digest = from_hex(sha256_bytes(payload));
    std::vector<std::uint8_t> sig(64);
    ULONG n = 0;
    if (BCryptSignHash(key.h, nullptr, digest.data(), 32, sig.data(), 64, &n, 0) < 0) throw GrError("Signing failed");
    sig.resize(n);
    return json{{"key_id", keyId}, {"payload", payload}, {"signature", to_hex(sig)}}.dump(2);
}

std::string catalog_payload_json(const Catalog& c) {
    json j{{"schema", 1}, {"serial", c.serial}, {"sets", json::array()}, {"revoked", c.revoked}, {"min_safe", c.min_safe}};
    for (auto& e : c.entries)
        j["sets"].push_back({{"set_id", e.set_id}, {"family", e.family}, {"category", category_name(e.category)}, {"channel", channel_name(e.channel)},
                             {"version", e.version}, {"release", e.release}, {"api_family", e.api_family}, {"api_major", e.api_major},
                             {"manifest_sha256", e.manifest_sha256}, {"auto_ok", e.auto_ok}});
    return j.dump();
}
}

CatalogResult verify_catalog(std::string_view text, const TrustStore& trust) {
    CatalogResult r;
    try {
        auto outer = json::parse(text);
        check_keys(outer, {"key_id", "payload", "signature"}, "catalog");
        if (!outer.contains("key_id") || !outer.contains("payload") || !outer.contains("signature") || !outer["key_id"].is_string() || !outer["payload"].is_string() || !outer["signature"].is_string())
            throw GrError("Catalog envelope is incomplete");
        r.keyId = outer["key_id"].get<std::string>();
        auto it = trust.keys.find(r.keyId);
        if (it == trust.keys.end()) { r.message = "Catalog is signed with a key this build does not trust"; return r; }
        auto payload = outer["payload"].get<std::string>();
        auto sig = from_hex(outer["signature"].get<std::string>());
        if (sig.size() != 64) { r.message = "Catalog signature has the wrong size"; return r; }
        Alg alg;
        Key key;
        if (BCryptImportKeyPair(alg.h, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key.h, const_cast<PUCHAR>(it->second.data()), static_cast<ULONG>(it->second.size()), 0) < 0) { r.message = "Trusted key is unusable"; return r; }
        auto digest = from_hex(sha256_bytes(payload));
        if (BCryptVerifySignature(key.h, nullptr, digest.data(), 32, sig.data(), 64, 0) < 0) { r.message = "Catalog signature does not match its content"; return r; }
        auto j = json::parse(payload);
        check_keys(j, {"schema", "serial", "sets", "revoked", "min_safe"}, "catalog payload");
        if (!j.contains("schema") || j["schema"] != 1) throw GrError("Unsupported catalog schema");
        r.catalog.serial = j.at("serial").get<std::uint64_t>();
        for (auto& s : j.at("sets")) {
            check_keys(s, {"set_id", "family", "category", "channel", "version", "release", "api_family", "api_major", "manifest_sha256", "auto_ok"}, "catalog entry");
            CatalogEntry e;
            e.set_id = s.at("set_id").get<std::string>();
            e.family = s.at("family").get<std::string>();
            auto cat = parse_category(s.at("category").get<std::string>());
            auto ch = parse_channel(s.at("channel").get<std::string>());
            if (!cat || !ch) throw GrError("Catalog entry has an unknown category or channel");
            e.category = *cat;
            e.channel = *ch;
            e.version = s.at("version").get<std::string>();
            if (!parse_version(e.version)) throw GrError("Catalog entry has a bad version");
            e.release = s.at("release").get<std::string>();
            e.api_family = s.at("api_family").get<std::string>();
            e.api_major = s.at("api_major").get<int>();
            e.manifest_sha256 = s.at("manifest_sha256").get<std::string>();
            e.auto_ok = s.at("auto_ok").get<bool>();
            r.catalog.entries.push_back(std::move(e));
        }
        for (auto& v : j.at("revoked")) r.catalog.revoked.push_back(v.get<std::string>());
        for (auto it2 = j.at("min_safe").begin(); it2 != j.at("min_safe").end(); ++it2) {
            if (!parse_version(it2.value().get<std::string>())) throw GrError("min_safe has a bad version");
            r.catalog.min_safe[it2.key()] = it2.value().get<std::string>();
        }
        r.ok = true;
    } catch (const std::exception& e) {
        r.ok = false;
        r.message = std::string("Catalog rejected: ") + e.what();
    }
    return r;
}

std::optional<std::string> FileCatalogProvider::fetch() {
    std::error_code ec;
    if (!fs::is_regular_file(path_, ec)) return std::nullopt;
    return read_text(path_);
}

std::optional<fs::path> LocalDirectoryPackageSource::fetch(const CatalogEntry& e) {
    std::string why;
    if (!safe_relative_path(e.set_id, why)) return std::nullopt;
    std::error_code ec;
    auto folder = dir_ / from_utf8(e.set_id);
    if (fs::is_directory(folder, ec)) return folder;
    auto zip = dir_ / from_utf8(e.set_id + ".zip");
    if (fs::is_regular_file(zip, ec)) return zip;
    return std::nullopt;
}

}
