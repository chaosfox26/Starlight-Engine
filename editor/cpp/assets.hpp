#pragma once
#include "geometry.hpp"
#include "../../deep/engine/model/texture_limits.h"
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
namespace starlight::editor {
constexpr size_t MaxImportBytes = 256ull * 1024 * 1024;
std::vector<MeshPart> parse_glb(std::string_view bytes);
std::vector<MeshPart> load_glb(const std::filesystem::path &);
void validate_model_bytes(std::string_view bytes);
std::string import_model_bytes(std::string_view bytes);
std::string import_model(const std::filesystem::path &);
std::shared_ptr<const std::vector<MeshPart>> load_model(const std::string &id);
struct ModelServiceStats {
    uint64_t hits = 0, misses = 0, evicted = 0, pinned = 0;
    size_t entries = 0, bytes = 0;
};
ModelServiceStats model_service_stats();
void model_service_trim();
struct TextureInfo {
    bool png = false;
    uint32_t width = 0, height = 0;
};
TextureInfo describe_texture(std::string_view bytes);
std::string import_texture_bytes(std::string_view bytes);
std::string import_texture(const std::filesystem::path &);
std::shared_ptr<const std::string> load_texture(const std::string &id);
std::shared_ptr<const std::string> load_model_bytes(const std::string &id);
struct TextureDecoded {
    uint32_t width = 0, height = 0;
    std::string container, pixelFormat;
    uint32_t frames = 0;
    uint64_t decodedBytes = 0, pixelStreamBytes = 0;
};
uint32_t crc32_bytes(std::string_view bytes);
uint64_t validate_png_stream(std::string_view bytes);
void validate_jpeg_stream(std::string_view bytes);
constexpr uint32_t MaxRuntimeTextureSide = starlight::texture_limits::MaxSide;
constexpr uint64_t MaxPngPixelStreamBytes = starlight::texture_limits::MaxPngPixelStreamBytes;
bool texture_decoder_available();
TextureDecoded decode_texture_bytes(std::string_view bytes);
TextureDecoded decode_texture(const std::string &id);
constexpr size_t MaxBlobBytes = 64ull * 1024 * 1024;
constexpr const char *UvAtlasType = "uv-atlas/float32x2/v1";
using BlobValidator = std::function<void(std::string_view data)>;
void register_blob_type(const std::string &type, BlobValidator validator);
bool blob_type_known(const std::string &type);
void validate_blob_type_name(const std::string &type);
void validate_uv_atlas_data(std::string_view data);
struct BlobData {
    std::string type;
    uint32_t version = 0;
    std::shared_ptr<const std::string> envelope;
    std::string_view data;
};
BlobData parse_blob(std::shared_ptr<const std::string> envelope);
std::string import_blob_bytes(std::string_view type, std::string_view data);
std::string import_blob(const std::filesystem::path &, std::string_view type);
BlobData load_blob(const std::string &id);
size_t model_uv_vertex_count(std::string_view glb);
size_t validate_uv_atlas_for_model(const std::string &blobId, const std::string &modelId);
struct AssetReadiness {
    bool ready = false;
    std::string detail;
};
AssetReadiness check_asset_ready(const std::string &id);
}
