#include "assets.hpp"
#include "engine_assets.hpp"
#include "platform.hpp"
#include <DirectXMath.h>
#include <atomic>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
namespace starlight::editor {
using namespace DirectX;
namespace {
template <class T> T read_at(std::string_view bytes, size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
        throw std::runtime_error("Truncated GLB data");
    T result;
    std::memcpy(&result, bytes.data() + offset, sizeof(T));
    return result;
}
size_t natural(const json &j) {
    auto n = native_int(j);
    if (n < 0)
        throw std::runtime_error("Negative GLB offset or count");
    return size_t(n);
}
double srgb(double v) {
    v = std::clamp(v, 0., 1.);
    return std::round((v <= .0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - .055) * 255) / 255;
}
// Walks the whole document with an explicit work list instead of recursion: the JSON parser accepts arbitrarily deep nesting without recursing, so a
// recursive walk would let a small crafted file exhaust the call stack. Every value is still visited, and any "uri" that is not an embedded data URI is refused.
void embedded_uris(const json &root) {
    std::vector<const json *> pending{&root};
    while (!pending.empty()) {
        const json *value = pending.back();
        pending.pop_back();
        if (value->is_object()) {
            for (auto it = value->begin(); it != value->end(); ++it) {
                if (it.key() == "uri" && (!it.value().is_string() || !it.value().get<std::string>().starts_with("data:")))
                    throw std::runtime_error("Models must embed their resources; external URI found");
                pending.push_back(&it.value());
            }
        } else if (value->is_array())
            for (const auto &item : *value)
                pending.push_back(&item);
    }
}
struct ModelEntry {
    std::shared_ptr<const std::vector<MeshPart>> parts;
    size_t bytes = 0;
    uint64_t used = 0;
};
// One parse in progress: threads that ask for the same model meanwhile wait for its result instead of parsing their own copy.
struct ModelParse {
    std::promise<std::shared_ptr<const std::vector<MeshPart>>> promise;
    std::shared_future<std::shared_ptr<const std::vector<MeshPart>>> result{promise.get_future()};
};
struct ModelService {
    std::mutex lock;
    std::unordered_map<std::string, ModelEntry> entries;
    std::unordered_map<std::string, std::shared_ptr<ModelParse>> parsing;
    size_t bytes = 0;
    uint64_t clock = 0;
    std::atomic<uint64_t> hits{0}, misses{0}, evicted{0}, pinned{0};
};
ModelService &model_service() {
    static ModelService service;
    return service;
}
constexpr size_t kModelEntries = 256, kModelBytes = 512ull << 20;
size_t model_bytes(const std::vector<MeshPart> &parts) {
    size_t total = 0;
    for (const auto &part : parts)
        total += part.mesh.vertices.size() * sizeof(Vertex) + part.mesh.indices.size() * sizeof(int);
    return total;
}
void evict_models(ModelService &s, const std::string &keep, size_t entryLimit, size_t byteLimit) {
    std::vector<std::pair<uint64_t, std::string>> order;
    order.reserve(s.entries.size());
    for (const auto &[id, entry] : s.entries) {
        if (id == keep) continue;
        if (entry.parts.use_count() > 1) { s.pinned.fetch_add(1, std::memory_order_relaxed); continue; }
        order.emplace_back(entry.used, id);
    }
    std::sort(order.begin(), order.end());
    for (const auto &item : order) {
        if (s.bytes <= byteLimit && s.entries.size() <= entryLimit) break;
        auto found = s.entries.find(item.second);
        s.bytes -= found->second.bytes;
        s.entries.erase(found);
        s.evicted.fetch_add(1, std::memory_order_relaxed);
    }
}
uint32_t big_endian(std::string_view bytes, size_t at) {
    if (bytes.size() < at + 4) throw std::runtime_error("Truncated image header");
    return (uint32_t(uint8_t(bytes[at])) << 24) | (uint32_t(uint8_t(bytes[at + 1])) << 16) | (uint32_t(uint8_t(bytes[at + 2])) << 8) | uint32_t(uint8_t(bytes[at + 3]));
}
} // namespace
std::vector<MeshPart> load_glb(const fs::path &path) {
    if (fs::file_size(path) > MaxImportBytes)
        throw std::runtime_error("Preview GLB exceeds 256 MiB");
    return parse_glb(read_file(path));
}
std::vector<MeshPart> parse_glb(std::string_view bytes, std::string *skipped) {
    if (bytes.size() > MaxImportBytes)
        throw std::runtime_error("Preview GLB exceeds 256 MiB");
    if (read_at<uint32_t>(bytes, 0) != 0x46546c67 || read_at<uint32_t>(bytes, 4) != 2 ||
        read_at<uint32_t>(bytes, 8) != bytes.size())
        throw std::runtime_error("Invalid GLB 2 header");
    std::string_view description, bin;
    for (size_t offset = 12; offset < bytes.size();) {
        auto length = read_at<uint32_t>(bytes, offset), kind = read_at<uint32_t>(bytes, offset + 4);
        offset += 8;
        if (length > bytes.size() - offset)
            throw std::runtime_error("Invalid GLB chunk range");
        auto data = bytes.substr(offset, length);
        if (kind == 0x4e4f534a) {
            if (!description.empty())
                throw std::runtime_error("Duplicate GLB JSON chunk");
            description = data;
        }
        if (kind == 0x004e4942)
            bin = data;
        offset += length;
    }
    if (description.empty() || bin.empty())
        throw std::runtime_error("GLB needs description and embedded geometry");
    auto root = json::parse(description);
    struct Access {
        size_t offset, stride, count;
        int component;
    };
    auto access = [&](size_t index, int dimensions) {
        const auto &a = root.at("accessors").at(index);
        if (a.contains("sparse"))
            throw std::runtime_error("Sparse GLB geometry unsupported by preview");
        if (a.at("type") != (dimensions == 3 ? "VEC3" : "SCALAR"))
            throw std::runtime_error("Invalid GLB accessor type");
        const auto &v = root.at("bufferViews").at(natural(a.at("bufferView")));
        if (v.contains("buffer") && natural(v.at("buffer")) != 0)
            throw std::runtime_error("External GLB geometry unsupported");
        int component = native_int(a.at("componentType"));
        size_t width = component == 5126 || component == 5125 ? 4
                       : component == 5123                    ? 2
                       : component == 5121                    ? 1
                                                              : 0;
        if (!width)
            throw std::runtime_error("Unsupported GLB component");
        size_t vo = v.contains("byteOffset") ? natural(v.at("byteOffset")) : 0,
               ao = a.contains("byteOffset") ? natural(a.at("byteOffset")) : 0,
               length = natural(v.at("byteLength")), count = natural(a.at("count")),
               element = width * dimensions,
               stride = v.contains("byteStride") ? natural(v.at("byteStride")) : element;
        if (vo > bin.size() || length > bin.size() - vo || ao > length || count > 6000000 ||
            stride < element || stride % width ||
            (count && (element > length - ao || (count - 1) > (length - ao - element) / stride)))
            throw std::runtime_error("GLB accessor exceeds buffer or preview limit");
        return Access{vo + ao, stride, count, component};
    };
    std::vector<MeshPart> parts;
    size_t total = 0, indexTotal = 0, skippedModes[5] = {};
    Vec3 minimum{INFINITY, INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY, -INFINITY};
    auto mesh = [&](size_t index, FXMMATRIX matrix) {
        for (const auto &primitive : root.at("meshes").at(index).at("primitives")) {
            const auto mode = primitive.contains("mode") ? native_int(primitive.at("mode")) : 4;
            if (mode < 4 || mode > 6) {
                ++skippedModes[mode >= 0 && mode < 4 ? mode : 4];
                continue;
            }
            auto a = access(natural(primitive.at("attributes").at("POSITION")), 3);
            if (a.component != 5126 || a.count > 2000000 - total)
                throw std::runtime_error("Preview needs float positions within two million vertices");
            total += a.count;
            MeshPart part;
            part.color = {183. / 255, 198. / 255, 224. / 255};
            for (size_t i = 0; i < a.count; i++) {
                size_t o = a.offset + i * a.stride;
                auto p = XMVector3Transform(XMVectorSet(read_at<float>(bin, o), read_at<float>(bin, o + 4),
                                                        read_at<float>(bin, o + 8), 1),
                                            matrix);
                XMFLOAT3 q;
                XMStoreFloat3(&q, p);
                Vec3 v{q.x, q.y, q.z};
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                    throw std::runtime_error("Invalid model coordinates");
                part.mesh.vertices.push_back({v});
                minimum = {std::min(minimum.x, v.x), std::min(minimum.y, v.y), std::min(minimum.z, v.z)};
                maximum = {std::max(maximum.x, v.x), std::max(maximum.y, v.y), std::max(maximum.z, v.z)};
            }
            if (primitive.contains("indices")) {
                auto ix = access(natural(primitive.at("indices")), 1);
                if (ix.count > 6000000 - indexTotal)
                    throw std::runtime_error("GLB primitives need more than six million indices in total");
                indexTotal += ix.count;
                for (size_t i = 0; i < ix.count; i++) {
                    size_t o = ix.offset + i * ix.stride;
                    uint32_t n;
                    if (ix.component == 5121)
                        n = read_at<uint8_t>(bin, o);
                    else if (ix.component == 5123)
                        n = read_at<uint16_t>(bin, o);
                    else if (ix.component == 5125)
                        n = read_at<uint32_t>(bin, o);
                    else
                        throw std::runtime_error("Invalid GLB index type");
                    if (n >= a.count)
                        throw std::runtime_error("GLB index outside positions");
                    part.mesh.indices.push_back(int(n));
                }
            } else
                for (size_t i = 0; i < a.count; i++)
                    part.mesh.indices.push_back(int(i));
            if (mode != 4) {
                const auto source = std::move(part.mesh.indices);
                part.mesh.indices.clear();
                for (size_t i = 0; i + 2 < source.size(); i++) {
                    const size_t odd = i & 1;
                    if (mode == 5) {
                        part.mesh.indices.push_back(source[i]);
                        part.mesh.indices.push_back(source[i + 1 + odd]);
                        part.mesh.indices.push_back(source[i + 2 - odd]);
                    } else {
                        part.mesh.indices.push_back(source[i + 1]);
                        part.mesh.indices.push_back(source[i + 2]);
                        part.mesh.indices.push_back(source[0]);
                    }
                }
            }
            part.mesh.validate();
            if (primitive.contains("material")) {
                const auto &mat = root.at("materials").at(natural(primitive.at("material")));
                if (mat.contains("pbrMetallicRoughness")) {
                    const auto &pbr = mat.at("pbrMetallicRoughness");
                    if (pbr.contains("baseColorFactor")) {
                        auto f = pbr.at("baseColorFactor");
                        part.color = {srgb(f.at(0).get<double>()), srgb(f.at(1).get<double>()),
                                      srgb(f.at(2).get<double>())};
                    }
                }
            }
            parts.push_back(std::move(part));
        }
    };
    std::unordered_set<size_t> visited;
    std::function<void(size_t, const XMMATRIX &, int)> node = [&](size_t id, const XMMATRIX &parent,
                                                                  int depth) {
        if (depth > 256 || !visited.insert(id).second)
            throw std::runtime_error("Repeated, cyclic or too-deep model node");
        const auto &n = root.at("nodes").at(id);
        auto local = XMMatrixIdentity();
        if (n.contains("matrix")) {
            auto m = n.at("matrix").get<std::vector<float>>();
            if (m.size() != 16)
                throw std::runtime_error("Invalid node matrix");
            XMFLOAT4X4 f;
            std::memcpy(&f, m.data(), 64);
            local = XMLoadFloat4x4(&f);
        } else {
            if (n.contains("scale")) {
                const auto &s = n.at("scale");
                local *= XMMatrixScaling(s.at(0).get<float>(), s.at(1).get<float>(), s.at(2).get<float>());
            }
            if (n.contains("rotation")) {
                const auto &r = n.at("rotation");
                local *= XMMatrixRotationQuaternion(XMVectorSet(r.at(0).get<float>(), r.at(1).get<float>(),
                                                                r.at(2).get<float>(), r.at(3).get<float>()));
            }
            if (n.contains("translation")) {
                const auto &t = n.at("translation");
                local *=
                    XMMatrixTranslation(t.at(0).get<float>(), t.at(1).get<float>(), t.at(2).get<float>());
            }
        }
        auto world = local * parent;
        if (n.contains("mesh"))
            mesh(natural(n.at("mesh")), world);
        if (n.contains("children"))
            for (const auto &child : n.at("children"))
                node(natural(child), world, depth + 1);
    };
    for (const auto &n : root.at("scenes").at(root.contains("scene") ? natural(root.at("scene")) : 0).at("nodes"))
        node(natural(n), XMMatrixIdentity(), 0);
    std::string note;
    {
        static const char *const kinds[5] = {"POINTS", "LINES", "LINE_LOOP", "LINE_STRIP", "unknown-mode"};
        for (int k = 0; k < 5; k++)
            if (skippedModes[k])
                note += (note.empty() ? "primitives not drawn (only triangles, strips and fans are loaded): " : ", ") + std::to_string(skippedModes[k]) + " " + kinds[k];
    }
    if (skipped) *skipped = note;
    if (!total || maximum.y - minimum.y < .0001)
        throw std::runtime_error("Model has no triangle geometry or height" + (note.empty() ? std::string() : "; " + note));
    double scale = 1.8 / (maximum.y - minimum.y), cx = (minimum.x + maximum.x) / 2,
           cz = (minimum.z + maximum.z) / 2;
    for (auto &part : parts) {
        for (auto &v : part.mesh.vertices)
            v.position = {(v.position.x - cx) * scale, (v.position.y - minimum.y) * scale,
                          (v.position.z - cz) * scale};
        part.mesh.normals();
    }
    return parts;
}
std::string model_skipped_note(std::string_view bytes) {
    std::string note;
    (void)parse_glb(bytes, &note);
    return note;
}
void validate_model_bytes(std::string_view bytes) {
    (void)parse_glb(bytes);
    for (size_t offset = 12; offset + 8 <= bytes.size();) {
        uint32_t count, kind;
        std::memcpy(&count, bytes.data() + offset, 4);
        std::memcpy(&kind, bytes.data() + offset + 4, 4);
        offset += 8;
        if (count > bytes.size() - offset) throw std::runtime_error("Truncated GLB chunk");
        if (kind == 0x4e4f534a) embedded_uris(json::parse(bytes.substr(offset, count)));
        offset += count;
    }
}
std::string import_model_bytes(std::string_view bytes) {
    validate_model_bytes(bytes);
    return EngineAssets::put(AssetKind::Model, bytes);
}
std::string import_model(const fs::path &path) {
    try {
        std::error_code error;
        const auto size = fs::file_size(path, error);
        if (error) throw std::runtime_error("The file cannot be read (" + error.message() + ")");
        if (size > MaxImportBytes) throw std::runtime_error("The model exceeds 256 MiB");
        return import_model_bytes(read_file(path));
    } catch (const std::exception &e) {
        throw std::runtime_error("Cannot import the model \"" + utf8(path.filename().wstring()) + "\": " + e.what());
    }
}
std::shared_ptr<const std::vector<MeshPart>> load_model(const std::string &id) {
    auto &s = model_service();
    if (!EngineAssets::valid_id(AssetKind::Model, id)) throw AssetMissing("Object refers to an invalid model asset id: " + id);
    std::shared_ptr<ModelParse> running, mine;
    {
        std::lock_guard guard(s.lock);
        auto found = s.entries.find(id);
        if (found != s.entries.end()) {
            found->second.used = ++s.clock;
            s.hits.fetch_add(1, std::memory_order_relaxed);
            return found->second.parts;
        }
        auto parse = s.parsing.find(id);
        if (parse != s.parsing.end()) running = parse->second;
        else s.parsing.emplace(id, mine = std::make_shared<ModelParse>());
    }
    if (running) {
        s.hits.fetch_add(1, std::memory_order_relaxed);
        return running->result.get();
    }
    s.misses.fetch_add(1, std::memory_order_relaxed);
    std::shared_ptr<const std::vector<MeshPart>> built;
    try {
        auto payload = EngineAssets::get(AssetKind::Model, id);
        built = std::make_shared<const std::vector<MeshPart>>(parse_glb(*payload));
        std::lock_guard guard(s.lock);
        auto &slot = s.entries[id]; // only the thread that owns the parse of this id ever inserts it
        slot.parts = built;
        slot.bytes = model_bytes(*built);
        slot.used = ++s.clock;
        s.bytes += slot.bytes;
        if (s.bytes > kModelBytes || s.entries.size() > kModelEntries) evict_models(s, id, kModelEntries / 4 * 3, kModelBytes / 4 * 3);
        s.parsing.erase(id);
    } catch (...) {
        {
            std::lock_guard guard(s.lock);
            s.parsing.erase(id);
        }
        mine->promise.set_exception(std::current_exception()); // waiting threads see the same failure each would have had parsing it themselves
        throw;
    }
    mine->promise.set_value(built);
    return built;
}
ModelServiceStats model_service_stats() {
    auto &s = model_service();
    std::lock_guard guard(s.lock);
    return {s.hits.load(), s.misses.load(), s.evicted.load(), s.pinned.load(), s.entries.size(), s.bytes};
}
void model_service_trim() {
    auto &s = model_service();
    std::lock_guard guard(s.lock);
    evict_models(s, {}, 0, 0);
}
TextureInfo describe_texture(std::string_view bytes) {
    TextureInfo info;
    static constexpr unsigned char pngSignature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (bytes.size() >= 8 && !std::memcmp(bytes.data(), pngSignature, 8)) {
        if (bytes.size() < 8 + 25 + 12 || big_endian(bytes, 8) != 13 || bytes.substr(12, 4) != "IHDR")
            throw std::runtime_error("The PNG header is damaged");
        info.png = true;
        info.width = big_endian(bytes, 16);
        info.height = big_endian(bytes, 20);
        const auto depth = uint8_t(bytes[24]), colour = uint8_t(bytes[25]);
        const bool depthOk = depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
        const bool colourOk = colour == 0 || colour == 2 || colour == 3 || colour == 4 || colour == 6;
        if (!depthOk || !colourOk) throw std::runtime_error("The PNG uses an unsupported bit depth or colour type");
        const auto tail = bytes.substr(bytes.size() - 12);
        if (tail.substr(4, 4) != "IEND" || big_endian(tail, 0) != 0) throw std::runtime_error("The PNG does not end with its IEND end marker at the last 12 bytes: it is truncated, or it carries bytes after the end marker, which Starlight does not accept");
    } else if (bytes.size() >= 4 && uint8_t(bytes[0]) == 0xff && uint8_t(bytes[1]) == 0xd8 && uint8_t(bytes[2]) == 0xff) {
        size_t at = 2;
        bool found = false;
        while (at + 4 <= bytes.size()) {
            if (uint8_t(bytes[at]) != 0xff) throw std::runtime_error("The JPEG segment layout is damaged");
            while (at + 1 < bytes.size() && uint8_t(bytes[at + 1]) == 0xff) ++at;
            if (at + 1 >= bytes.size()) throw std::runtime_error("The JPEG is truncated");
            const auto marker = uint8_t(bytes[at + 1]);
            if (marker == 0xda || marker == 0xd9) break;
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd8)) { at += 2; continue; }
            if (at + 4 > bytes.size()) break;
            const size_t length = (size_t(uint8_t(bytes[at + 2])) << 8) | uint8_t(bytes[at + 3]);
            if (length < 2 || at + 2 + length > bytes.size()) throw std::runtime_error("The JPEG is truncated");
            if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
                if (length < 8) throw std::runtime_error("The JPEG frame header is damaged");
                info.height = (uint32_t(uint8_t(bytes[at + 5])) << 8) | uint8_t(bytes[at + 6]);
                info.width = (uint32_t(uint8_t(bytes[at + 7])) << 8) | uint8_t(bytes[at + 8]);
                found = true;
                break;
            }
            at += 2 + length;
        }
        if (!found) throw std::runtime_error("The JPEG has no frame header");
        size_t end = bytes.size();
        while (end > 2 && bytes[end - 1] == 0) --end;
        if (end < 2 || uint8_t(bytes[end - 2]) != 0xff || uint8_t(bytes[end - 1]) != 0xd9) throw std::runtime_error("The JPEG is truncated (no end marker)");
    } else
        throw std::runtime_error("Only PNG and JPEG textures can be imported");
    const auto fit = texture_limits::checkDecodedRgba8(info.width, info.height);
    if (!fit.ok())
        throw std::runtime_error("The texture is refused: " + texture_limits::explain(fit, info.width, info.height, texture_limits::DecodedChannels, texture_limits::DecodedBytesPerChannel));
    return info;
}
std::string import_texture_bytes(std::string_view bytes) {
    if (bytes.size() > MaxImportBytes) throw std::runtime_error("The texture exceeds 256 MiB");
    const auto info = describe_texture(bytes);
    try {
        if (info.png) (void)validate_png_stream(bytes);
    } catch (const std::exception &e) {
        throw std::runtime_error(std::string("The image fails the Starlight image integrity check and is not stored: ") + e.what());
    }
    return EngineAssets::put(AssetKind::Texture, bytes);
}
std::string import_texture(const fs::path &path) {
    try {
        std::error_code error;
        const auto size = fs::file_size(path, error);
        if (error) throw std::runtime_error("The file cannot be read (" + error.message() + ")");
        if (size > MaxImportBytes) throw std::runtime_error("The texture exceeds 256 MiB");
        return import_texture_bytes(read_file(path));
    } catch (const std::exception &e) {
        throw std::runtime_error("Cannot import the texture \"" + utf8(path.filename().wstring()) + "\": " + e.what());
    }
}
std::shared_ptr<const std::string> load_texture(const std::string &id) {
    return EngineAssets::get(AssetKind::Texture, id);
}
} // namespace starlight::editor
