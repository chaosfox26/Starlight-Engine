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

inline LoadError readWholeFile(const std::string& path, std::vector<uint8_t>& bytes) {
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
