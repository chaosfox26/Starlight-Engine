#include "assets.hpp"
#include "platform.hpp"
#include <DirectXMath.h>
#include <cstring>
#include <functional>
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
} // namespace
std::vector<MeshPart> load_glb(const fs::path &path) {
    if (fs::file_size(path) > 256ull * 1024 * 1024)
        throw std::runtime_error("Preview GLB exceeds 256 MiB");
    auto storage = read_file(path);
    std::string_view bytes = storage;
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
    size_t total = 0;
    Vec3 minimum{INFINITY, INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY, -INFINITY};
    auto mesh = [&](size_t index, FXMMATRIX matrix) {
        for (const auto &primitive : root.at("meshes").at(index).at("primitives")) {
            if (primitive.contains("mode") && native_int(primitive.at("mode")) != 4)
                continue;
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
    if (!total || maximum.y - minimum.y < .0001)
        throw std::runtime_error("Model has no triangle geometry or height");
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
} // namespace starlight::editor
