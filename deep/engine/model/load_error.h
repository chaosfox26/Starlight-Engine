#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <system_error>
#include <vector>

namespace model {

struct ByteView {
    const uint8_t* data = nullptr;
    size_t size = 0;
    ByteView() = default;
    ByteView(const uint8_t* bytes, size_t count) : data(bytes), size(count) {}
    ByteView(const std::vector<uint8_t>& bytes) : data(bytes.data()), size(bytes.size()) {}
};

enum class LoadCode : uint32_t {
    Ok = 0,
    InvalidArgument,
    NotFound,
    IoError,
    Truncated,
    Malformed,
    Unsupported,
    TooLarge,
    DecoderUnavailable,
    DecodeFailed,
    NoGeometry,
    SkeletonInvalid,
    AtlasSizeMismatch,
    AtlasNotFinite,
    OutOfMemory,
    Internal
};

inline const char* loadCodeName(LoadCode code) {
    switch (code) {
    case LoadCode::Ok: return "Ok";
    case LoadCode::InvalidArgument: return "InvalidArgument";
    case LoadCode::NotFound: return "NotFound";
    case LoadCode::IoError: return "IoError";
    case LoadCode::Truncated: return "Truncated";
    case LoadCode::Malformed: return "Malformed";
    case LoadCode::Unsupported: return "Unsupported";
    case LoadCode::TooLarge: return "TooLarge";
    case LoadCode::DecoderUnavailable: return "DecoderUnavailable";
    case LoadCode::DecodeFailed: return "DecodeFailed";
    case LoadCode::NoGeometry: return "NoGeometry";
    case LoadCode::SkeletonInvalid: return "SkeletonInvalid";
    case LoadCode::AtlasSizeMismatch: return "AtlasSizeMismatch";
    case LoadCode::AtlasNotFinite: return "AtlasNotFinite";
    case LoadCode::OutOfMemory: return "OutOfMemory";
    case LoadCode::Internal: return "Internal";
    }
    return "Unknown";
}

struct LoadError {
    LoadCode code = LoadCode::Ok;
    std::string message;
    bool ok() const { return code == LoadCode::Ok; }
    bool failed() const { return code != LoadCode::Ok; }
    std::string text() const { return ok() ? std::string() : std::string(loadCodeName(code)) + ": " + message; }
};

inline LoadError makeLoadError(LoadCode code, const std::string& label, const std::string& what) {
    LoadError e;
    e.code = code;
    e.message = label.empty() ? what : label + ": " + what;
    return e;
}

// Largest files read whole for each kind of model input: the limits of the engine asset store for models and textures (editor/cpp/engine_assets.cpp max_file_bytes),
// and the blob limit for an atlas UV sidecar.
inline constexpr uint64_t MaxModelFileBytes = uint64_t(512) << 20;
inline constexpr uint64_t MaxTextureFileBytes = uint64_t(512) << 20;
inline constexpr uint64_t MaxAtlasFileBytes = uint64_t(128) << 20;

// A file larger than `maxBytes` (1 GiB by default, the same ceiling as the editor's read_file) is refused before any memory is allocated for it.
inline LoadError readWholeFile(const std::string& path, std::vector<uint8_t>& bytes, uint64_t maxBytes = uint64_t(1) << 30) {
    bytes.clear();
    try {
        const std::filesystem::path p = std::filesystem::u8path(path);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(p, ec)) return makeLoadError(LoadCode::NotFound, path, "file not found");
        std::ifstream file(p, std::ios::binary);
        if (!file) return makeLoadError(LoadCode::IoError, path, "file could not be opened");
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0) return makeLoadError(LoadCode::IoError, path, "file size could not be determined");
        if (static_cast<uint64_t>(size) > maxBytes) return makeLoadError(LoadCode::TooLarge, path, "file is larger than " + std::to_string(maxBytes >> 20) + " MiB; it is not loaded whole");
        file.seekg(0, std::ios::beg);
        bytes.resize(static_cast<size_t>(size));
        if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
            bytes.clear();
            return makeLoadError(LoadCode::IoError, path, "file read failed");
        }
        return LoadError();
    } catch (const std::bad_alloc&) {
        bytes.clear();
        return makeLoadError(LoadCode::OutOfMemory, path, "not enough memory to read the file");
    } catch (...) {
        bytes.clear();
        return makeLoadError(LoadCode::IoError, path, "unexpected failure while reading the file");
    }
}

}
