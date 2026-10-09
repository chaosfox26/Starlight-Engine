// Starlight DX12 client — skinned character models (see skinned.h).
#include "skinned.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <unordered_map>
#include "../third_party/cgltf.h"

using namespace DirectX;

namespace model {

int SkinnedModel::clipIndex(const std::string& n) const {
    for (size_t i = 0; i < clips.size(); ++i) if (clips[i].name == n) return (int)i;
    return -1;
}
int SkinnedModel::jointIndex(const std::string& n) const {
    for (size_t i = 0; i < joints.size(); ++i) if (joints[i].name == n) return (int)i;
    return -1;
}

static XMMATRIX FromGltf(const float* m) {   // glTF column-major storage -> rows = columns (row-vector convention)
    return XMMatrixSet(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
}

// one-shot clips hold their last frame instead of wrapping
static bool ClipLoops(const std::string& n) {
    static const char* oneShot[] = { "jump", "double_jump", "land", "attack", "hit_react", "death", "bow_draw", "bow_release", "bow_tap", "bow_charge", "dodge" };
    for (const char* s : oneShot) if (n.rfind(s, 0) == 0) return false;
    return true;
}

static void GenerateTangents(SkinnedModel& m) {
    std::vector<XMFLOAT3> tan(m.verts.size(), XMFLOAT3(0, 0, 0)), bit(m.verts.size(), XMFLOAT3(0, 0, 0));
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const SkinVertex& a = m.verts[m.indices[i]]; const SkinVertex& b = m.verts[m.indices[i + 1]]; const SkinVertex& c = m.verts[m.indices[i + 2]];
        float e1x = b.px - a.px, e1y = b.py - a.py, e1z = b.pz - a.pz;
        float e2x = c.px - a.px, e2y = c.py - a.py, e2z = c.pz - a.pz;
        float du1 = b.u - a.u, dv1 = b.v - a.v, du2 = c.u - a.u, dv2 = c.v - a.v;
        float det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;
        float r = 1.0f / det;
        XMFLOAT3 t((e1x * dv2 - e2x * dv1) * r, (e1y * dv2 - e2y * dv1) * r, (e1z * dv2 - e2z * dv1) * r);
        XMFLOAT3 bt((e2x * du1 - e1x * du2) * r, (e2y * du1 - e1y * du2) * r, (e2z * du1 - e1z * du2) * r);
        for (int k = 0; k < 3; ++k) {
            uint32_t vi = m.indices[i + (size_t)k];
            tan[vi].x += t.x; tan[vi].y += t.y; tan[vi].z += t.z;
            bit[vi].x += bt.x; bit[vi].y += bt.y; bit[vi].z += bt.z;
        }
    }
    for (size_t i = 0; i < m.verts.size(); ++i) {
        SkinVertex& v = m.verts[i];
        XMVECTOR n = XMVectorSet(v.nx, v.ny, v.nz, 0), t = XMLoadFloat3(&tan[i]), b = XMLoadFloat3(&bit[i]);
        if (XMVectorGetX(XMVector3LengthSq(t)) < 1e-12f) {   // degenerate UVs: any perpendicular
            XMVECTOR ref = std::fabs(v.ny) < 0.9f ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
            t = XMVector3Cross(ref, n);
        }
        t = XMVector3Normalize(XMVectorSubtract(t, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, t)))));
        float w = XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), b)) < 0.0f ? -1.0f : 1.0f;
        XMFLOAT3 tf; XMStoreFloat3(&tf, t);
        v.tx = tf.x; v.ty = tf.y; v.tz = tf.z; v.tw = w;
    }
}

namespace {

struct GltfDeleter { void operator()(cgltf_data* d) const { cgltf_free(d); } };
using GltfPtr = std::unique_ptr<cgltf_data, GltfDeleter>;

cgltf_result RefuseExternalRead(const cgltf_memory_options*, const cgltf_file_options*, const char*, cgltf_size*, void**) { return cgltf_result_io_error; }
void IgnoreExternalRelease(const cgltf_memory_options*, const cgltf_file_options*, void*, cgltf_size) {}

std::string ModelNameOf(const std::string& label) {
    size_t slash = label.find_last_of("/\\");
    std::string name = label.substr(slash == std::string::npos ? 0 : slash + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".glb") == 0) name.resize(name.size() - 4);
    return name;
}

const char* GltfResultName(cgltf_result r) {
    switch (r) {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "legacy glTF 1.0";
    default: return "unknown error";
    }
}

LoadCode GltfResultCode(cgltf_result r) {
    switch (r) {
    case cgltf_result_data_too_short: return LoadCode::Truncated;
    case cgltf_result_unknown_format:
    case cgltf_result_legacy_gltf:
    case cgltf_result_file_not_found:
    case cgltf_result_io_error: return LoadCode::Unsupported;
    case cgltf_result_out_of_memory: return LoadCode::OutOfMemory;
    default: return LoadCode::Malformed;
    }
}

float ReadLittleEndianFloat(const uint8_t* p) {
    const uint32_t bits = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

LoadError CheckAtlas(const SkinnedModel& m, ByteView atlas, const std::string& label) {
    if (!atlas.data && atlas.size) return makeLoadError(LoadCode::InvalidArgument, label, "atlas UV span pointer is null");
    const size_t need = m.verts.size() * 2 * sizeof(float);
    if (atlas.size != need)
        return makeLoadError(LoadCode::AtlasSizeMismatch, label, "atlas UV span has " + std::to_string(atlas.size) + " bytes (" + std::to_string(atlas.size / 4) +
                             " float32 values) but the model has " + std::to_string(m.verts.size()) + " vertices and needs " + std::to_string(need) + " bytes (2 x float32 per vertex)");
    for (size_t i = 0; i < m.verts.size() * 2; ++i)
        if (!std::isfinite(ReadLittleEndianFloat(atlas.data + i * sizeof(float))))
            return makeLoadError(LoadCode::AtlasNotFinite, label, std::string("atlas UV value ") + std::to_string(i) + " (vertex " + std::to_string(i / 2) + ", " + ((i & 1) ? "V" : "U") + ") is not a finite number");
    return LoadError();
}

void WriteAtlas(SkinnedModel& m, ByteView atlas) {
    for (size_t i = 0; i < m.verts.size(); ++i) {
        m.verts[i].u = ReadLittleEndianFloat(atlas.data + (i * 2) * sizeof(float));
        m.verts[i].v = ReadLittleEndianFloat(atlas.data + (i * 2 + 1) * sizeof(float));
    }
    m.atlasUV = true;
}

bool SumOverflows(cgltf_size a, cgltf_size b) { return a > SIZE_MAX - b; }
bool ProductOverflows(cgltf_size a, cgltf_size b) { return b && a > SIZE_MAX / b; }

// cgltf_validate adds and multiplies these untrusted numbers without checking for wrap-around, so an offset, stride or count that wraps slips past its range tests and the vertex readers then run off the buffer.
bool BufferArithmeticWraps(const cgltf_data* data) {
    for (cgltf_size i = 0; i < data->buffer_views_count; ++i)
        if (SumOverflows(data->buffer_views[i].offset, data->buffer_views[i].size)) return true;
    for (cgltf_size i = 0; i < data->accessors_count; ++i) {
        const cgltf_accessor& a = data->accessors[i];
        const cgltf_size element = cgltf_calc_size(a.type, a.component_type);
        if (a.buffer_view && a.count) {
            if (ProductOverflows(a.stride, a.count - 1) || SumOverflows(a.offset, a.stride * (a.count - 1)) || SumOverflows(a.offset + a.stride * (a.count - 1), element)) return true;
        }
        if (a.is_sparse) {
            const cgltf_size indexSize = cgltf_component_size(a.sparse.indices_component_type);
            if (ProductOverflows(indexSize, a.sparse.count) || SumOverflows(a.sparse.indices_byte_offset, indexSize * a.sparse.count) ||
                ProductOverflows(element, a.sparse.count) || SumOverflows(a.sparse.values_byte_offset, element * a.sparse.count)) return true;
        }
    }
    return false;
}
// cgltf's tokenizer takes time quadratic in the nesting depth of the JSON, so a few hundred kilobytes of brackets would stall the load for minutes. Real glTF nests a dozen levels at most.
// Brackets are counted everywhere, string literals included: a damaged quote can desynchronise a scanner that tracks strings while the tokenizer still walks the nesting. Only a string holding hundreds of unmatched brackets is refused by mistake.
constexpr size_t kMaxJsonDepth = 256;
bool JsonNestsTooDeep(ByteView glb) {
    size_t begin = 0, end = glb.size;
    if (glb.size >= 20 && std::memcmp(glb.data, "glTF", 4) == 0) {
        uint32_t length;
        std::memcpy(&length, glb.data + 12, sizeof(length));
        begin = 20;
        end = length > glb.size - begin ? glb.size : begin + length;
    }
    size_t depth = 0;
    for (size_t i = begin; i < end; ++i) {
        const uint8_t c = glb.data[i];
        if (c == '[' || c == '{') { if (++depth > kMaxJsonDepth) return true; }
        else if ((c == ']' || c == '}') && depth) --depth;
    }
    return false;
}
LoadError LoadSkinnedImpl(ByteView glb, const ByteView* atlasUv, const std::string& label, SkinnedModel& out) {
    out = SkinnedModel();
    out.source = label;
    out.name = ModelNameOf(label);
    auto fail = [&](LoadCode code, const std::string& what) {
        SkinnedModel empty; empty.source = out.source; empty.name = out.name; out = std::move(empty);
        return makeLoadError(code, label, what);
    };
    if (glb.size == 0) return fail(LoadCode::Truncated, "GLB is empty");
    if (!glb.data) return fail(LoadCode::InvalidArgument, "GLB pointer is null");

    if (JsonNestsTooDeep(glb)) return fail(LoadCode::Malformed, "the JSON nests deeper than " + std::to_string(kMaxJsonDepth) + " levels");
    cgltf_options opt{};
    opt.file.read = &RefuseExternalRead;
    opt.file.release = &IgnoreExternalRelease;
    cgltf_data* parsed = nullptr;
    cgltf_result r = cgltf_parse(&opt, glb.data, glb.size, &parsed);
    if (r != cgltf_result_success) return fail(GltfResultCode(r), std::string("glTF parse failed: ") + GltfResultName(r));
    GltfPtr owner(parsed);
    cgltf_data* data = parsed;
    for (cgltf_size b = 0; b < data->buffers_count; ++b) {   // cgltf allocates byteLength before it decodes a base64 buffer, so a tiny URI could ask for gigabytes
        const char* uri = data->buffers[b].uri;
        const char* comma = uri && std::strncmp(uri, "data:", 5) == 0 ? std::strchr(uri, ',') : nullptr;
        if (comma && std::strlen(comma + 1) / 4 * 3 + 2 < data->buffers[b].size)
            return fail(LoadCode::Malformed, "glTF buffer " + std::to_string(b) + " declares " + std::to_string(data->buffers[b].size) + " bytes but its data URI holds fewer");
    }
    r = cgltf_load_buffers(&opt, data, "");
    if (r != cgltf_result_success)
        return fail(GltfResultCode(r), std::string("glTF buffers could not be loaded: ") + GltfResultName(r) +
                    (r == cgltf_result_io_error || r == cgltf_result_file_not_found ? " (external buffer files cannot be resolved from memory)" : ""));
    if (BufferArithmeticWraps(data)) return fail(LoadCode::Malformed, "an accessor or buffer view has an offset, stride or count so large that its size wraps around");
    r = cgltf_validate(data);
    if (r != cgltf_result_success)
        return fail(r == cgltf_result_data_too_short ? LoadCode::Malformed : GltfResultCode(r),
                    std::string("glTF validation failed: ") + (r == cgltf_result_data_too_short ? "an accessor, buffer view or index lies outside its buffer" : GltfResultName(r)));
    for (cgltf_size b = 0; b < data->buffers_count; ++b)
        if (!data->buffers[b].data) return fail(LoadCode::Malformed, "glTF buffer " + std::to_string(b) + " has no data (missing BIN chunk)");

    std::unordered_map<const cgltf_node*, int> jointOf;
    const cgltf_skin* skin = data->skins_count ? &data->skins[0] : nullptr;
    if (skin) {
        if (skin->joints_count > 256) return fail(LoadCode::SkeletonInvalid, "skin has " + std::to_string(skin->joints_count) + " joints; the 8-bit joint index addresses at most 256");
        if (skin->inverse_bind_matrices && skin->inverse_bind_matrices->count < skin->joints_count)
            return fail(LoadCode::Malformed, "inverse bind matrix accessor holds fewer matrices than the skin has joints");
        for (cgltf_size j = 0; j < skin->joints_count; ++j) jointOf[skin->joints[j]] = (int)j;
        for (cgltf_size j = 0; j < skin->joints_count; ++j) {
            const cgltf_node* n = skin->joints[j];
            Joint jt; jt.name = n->name ? n->name : ("joint" + std::to_string(j));
            XMMATRIX base = XMMatrixIdentity();
            for (const cgltf_node* a = n->parent; a; a = a->parent) {
                auto pit = jointOf.find(a);
                if (pit != jointOf.end()) { jt.parent = pit->second; break; }
                float local[16]; cgltf_node_transform_local(a, local);
                base = XMMatrixMultiply(base, FromGltf(local));
            }
            XMStoreFloat4x4(&jt.base, base);
            if (n->has_matrix) {
                XMVECTOR scale, rotation, translation;
                if (!XMMatrixDecompose(&scale, &rotation, &translation, FromGltf(n->matrix)))
                    return fail(LoadCode::SkeletonInvalid, "joint '" + jt.name + "' has a matrix transform that cannot be split into translation, rotation and scale");
                XMStoreFloat3(&jt.t, translation); XMStoreFloat4(&jt.r, rotation); XMStoreFloat3(&jt.s, scale);
            }
            if (n->has_translation) jt.t = XMFLOAT3(n->translation[0], n->translation[1], n->translation[2]);
            if (n->has_rotation) jt.r = XMFLOAT4(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]);
            if (n->has_scale) jt.s = XMFLOAT3(n->scale[0], n->scale[1], n->scale[2]);
            float ibm[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            if (skin->inverse_bind_matrices && !cgltf_accessor_read_float(skin->inverse_bind_matrices, j, ibm, 16))
                return fail(LoadCode::Malformed, "inverse bind matrix " + std::to_string(j) + " could not be read");
            XMStoreFloat4x4(&jt.ibm, FromGltf(ibm));
            out.joints.push_back(jt);
        }
        for (size_t j = 0; j < out.joints.size(); ++j)
            if (out.joints[j].parent >= (int)j) return fail(LoadCode::SkeletonInvalid, "skin joint order is not parent-first");
    }

    bool first = true;
    auto grow = [&](float x, float y, float z) {
        if (first) { out.bbMin = out.bbMax = XMFLOAT3(x, y, z); first = false; return; }
        out.bbMin.x = std::min(out.bbMin.x, x); out.bbMin.y = std::min(out.bbMin.y, y); out.bbMin.z = std::min(out.bbMin.z, z);
        out.bbMax.x = std::max(out.bbMax.x, x); out.bbMax.y = std::max(out.bbMax.y, y); out.bbMax.z = std::max(out.bbMax.z, z);
    };
    LoadError geometryError;
    int skippedPrimitives[5] = { 0, 0, 0, 0, 0 };   // indexed by cgltf_primitive_type: invalid, points, lines, line loop, line strip
    auto visit = [&](const cgltf_node* node) -> bool {
        if (!node->mesh) return true;
        float world[16]; cgltf_node_transform_world(node, world);
        XMMATRIX W = node->skin ? XMMatrixIdentity() : FromGltf(world);
        XMVECTOR determinant;
        XMMATRIX inverse = XMMatrixInverse(&determinant, W);
        XMMATRIX Wn = XMVectorGetX(determinant) == 0.0f || XMMatrixIsInfinite(inverse) || XMMatrixIsNaN(inverse) ? W : XMMatrixTranspose(inverse);   // a zero scale has no inverse; its normals fall back to the matrix itself instead of NaN
        std::string nodeName = node->name ? node->name : "";
        auto bad = [&](LoadCode code, cgltf_size pi, const std::string& what) {
            geometryError = makeLoadError(code, label, "node '" + nodeName + "' primitive " + std::to_string(pi) + ": " + what);
            return false;
        };
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive* prim = &node->mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles && prim->type != cgltf_primitive_type_triangle_strip && prim->type != cgltf_primitive_type_triangle_fan) {
                if ((int)prim->type < 5) ++skippedPrimitives[(int)prim->type];
                continue;
            }
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *jn = nullptr, *wt = nullptr;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                const cgltf_attribute& at = prim->attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) jn = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) wt = at.data;
            }
            if (!pos || !uv) continue;
            if (cgltf_num_components(pos->type) < 3) return bad(LoadCode::Malformed, pi, "POSITION has fewer than 3 components");
            if (cgltf_num_components(uv->type) < 2) return bad(LoadCode::Malformed, pi, "TEXCOORD_0 has fewer than 2 components");
            if (nrm && cgltf_num_components(nrm->type) < 3) return bad(LoadCode::Malformed, pi, "NORMAL has fewer than 3 components");
            if (jn && cgltf_num_components(jn->type) < 4) return bad(LoadCode::Malformed, pi, "JOINTS_0 has fewer than 4 components");
            if (wt && cgltf_num_components(wt->type) < 4) return bad(LoadCode::Malformed, pi, "WEIGHTS_0 has fewer than 4 components");
            if (uv->count != pos->count || (nrm && nrm->count != pos->count) || (jn && jn->count != pos->count) || (wt && wt->count != pos->count))
                return bad(LoadCode::Malformed, pi, "vertex attribute counts differ from POSITION (" + std::to_string(pos->count) + ")");
            if (out.verts.size() + pos->count > 0xFFFFFFFFull) return bad(LoadCode::TooLarge, pi, "vertex total exceeds the 32-bit index range");
            const cgltf_size triangleIndices = prim->indices ? prim->indices->count : pos->count;
            if (prim->type == cgltf_primitive_type_triangles && triangleIndices % 3 != 0) return bad(LoadCode::Malformed, pi, "index count " + std::to_string(triangleIndices) + " is not a multiple of 3");
            Submesh sm; sm.node = nodeName;
            if (prim->material) {
                sm.material = prim->material->name ? prim->material->name : "";
                if (prim->material->has_pbr_metallic_roughness) {
                    const float* bc = prim->material->pbr_metallic_roughness.base_color_factor;
                    sm.baseColor = XMFLOAT3(bc[0], bc[1], bc[2]);
                }
                sm.emissive = sm.material == "glow" || sm.material == "catchlight";
            }
            sm.firstIndex = (uint32_t)out.indices.size();
            uint32_t base = (uint32_t)out.verts.size();
            for (cgltf_size v = 0; v < pos->count; ++v) {
                float p[3] = { 0,0,0 }, nn[3] = { 0,1,0 }, t[2] = { 0,0 }, w[4] = { 1,0,0,0 };
                cgltf_uint j[4] = { 0,0,0,0 };
                if (!cgltf_accessor_read_float(pos, v, p, 3)) return bad(LoadCode::Malformed, pi, "POSITION element " + std::to_string(v) + " could not be read");
                if (nrm && !cgltf_accessor_read_float(nrm, v, nn, 3)) return bad(LoadCode::Malformed, pi, "NORMAL element " + std::to_string(v) + " could not be read");
                if (!cgltf_accessor_read_float(uv, v, t, 2)) return bad(LoadCode::Malformed, pi, "TEXCOORD_0 element " + std::to_string(v) + " could not be read");
                if (jn && !cgltf_accessor_read_uint(jn, v, j, 4)) return bad(LoadCode::Malformed, pi, "JOINTS_0 element " + std::to_string(v) + " could not be read");
                if (wt && !cgltf_accessor_read_float(wt, v, w, 4)) return bad(LoadCode::Malformed, pi, "WEIGHTS_0 element " + std::to_string(v) + " could not be read");
                const bool finite = std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) && std::isfinite(nn[0]) && std::isfinite(nn[1]) && std::isfinite(nn[2]) &&
                                    std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(w[0]) && std::isfinite(w[1]) && std::isfinite(w[2]) && std::isfinite(w[3]);
                if (!finite) return bad(LoadCode::Malformed, pi, "vertex " + std::to_string(v) + " has a non-finite position, normal, UV or weight");
                float ws = w[0] + w[1] + w[2] + w[3]; if (ws <= 0.0f) { ws = 1.0f; w[0] = 1.0f; }
                if (jn) for (int k = 0; k < 4; ++k)
                    if (w[k] > 0.0f && j[k] >= out.joints.size())
                        return bad(LoadCode::SkeletonInvalid, pi, "vertex " + std::to_string(v) + " uses joint " + std::to_string(j[k]) + " but the skin has " + std::to_string(out.joints.size()) + " joints");
                XMFLOAT3 fp, fn;
                XMStoreFloat3(&fp, XMVector3Transform(XMVectorSet(p[0], p[1], p[2], 1.0f), W));
                XMStoreFloat3(&fn, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(nn[0], nn[1], nn[2], 0.0f), Wn)));
                SkinVertex sv{};
                sv.px = fp.x; sv.py = fp.y; sv.pz = fp.z; sv.nx = fn.x; sv.ny = fn.y; sv.nz = fn.z;
                sv.u = t[0]; sv.v = t[1];
                for (int k = 0; k < 4; ++k) { sv.j[k] = (uint8_t)std::min<cgltf_uint>(j[k], 255u); sv.w[k] = w[k] / ws; }
                out.verts.push_back(sv);
                grow(fp.x, fp.y, fp.z);
            }
            std::vector<uint32_t> src;
            src.reserve(triangleIndices);
            if (prim->indices) {
                for (cgltf_size i = 0; i < prim->indices->count; ++i) {
                    const cgltf_size index = cgltf_accessor_read_index(prim->indices, i);
                    if (index >= pos->count) return bad(LoadCode::Malformed, pi, "index " + std::to_string(i) + " is " + std::to_string(index) + " but the primitive has " + std::to_string(pos->count) + " vertices");
                    src.push_back((uint32_t)index);
                }
            } else for (cgltf_size i = 0; i < pos->count; ++i) src.push_back((uint32_t)i);
            if (prim->type == cgltf_primitive_type_triangles) for (uint32_t s : src) out.indices.push_back(base + s);
            else if (prim->type == cgltf_primitive_type_triangle_strip)   // glTF: triangle i = v[i], v[i+1+i%2], v[i+2-i%2]
                for (size_t i = 0; i + 2 < src.size(); ++i) { const size_t odd = i & 1; out.indices.push_back(base + src[i]); out.indices.push_back(base + src[i + 1 + odd]); out.indices.push_back(base + src[i + 2 - odd]); }
            else   // fan: triangle i = v[i+1], v[i+2], v[0]
                for (size_t i = 0; i + 2 < src.size(); ++i) { out.indices.push_back(base + src[i + 1]); out.indices.push_back(base + src[i + 2]); out.indices.push_back(base + src[0]); }
            sm.indexCount = (uint32_t)out.indices.size() - sm.firstIndex;
            if (out.subs.size() >= 30) return bad(LoadCode::Unsupported, pi, "a model can draw at most 30 primitives; two instance slots are reserved for its reduced-detail meshes");
            out.subs.push_back(sm);
        }
        return true;
    };
    std::vector<const cgltf_node*> roots;
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene) for (cgltf_size i = 0; i < scene->nodes_count; ++i) roots.push_back(scene->nodes[i]);
    else for (cgltf_size i = 0; i < data->nodes_count; ++i) if (!data->nodes[i].parent) roots.push_back(&data->nodes[i]);
    std::vector<const cgltf_node*> pending;
    for (const cgltf_node* root : roots) {
        pending.push_back(root);
        while (!pending.empty()) {
            const cgltf_node* node = pending.back(); pending.pop_back();
            if (!visit(node)) { LoadError e = geometryError; SkinnedModel empty; empty.source = out.source; empty.name = out.name; out = std::move(empty); return e; }
            for (cgltf_size c = node->children_count; c-- > 0;) pending.push_back(node->children[c]);
        }
    }

    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& an = data->animations[ai];
        Clip c; c.name = an.name ? an.name : ("clip" + std::to_string(ai)); c.loop = ClipLoops(c.name);
        for (cgltf_size ci = 0; ci < an.channels_count; ++ci) {
            const cgltf_animation_channel& ch = an.channels[ci];
            auto it = jointOf.find(ch.target_node);
            if (it == jointOf.end() || !ch.sampler || !ch.sampler->input || !ch.sampler->output) continue;
            if (ch.sampler->input->count == 0) continue;
            Channel k; k.joint = it->second;
            int comps = 0;
            if (ch.target_path == cgltf_animation_path_type_translation) { k.path = 0; comps = 3; }
            else if (ch.target_path == cgltf_animation_path_type_rotation) { k.path = 1; comps = 4; }
            else if (ch.target_path == cgltf_animation_path_type_scale) { k.path = 2; comps = 3; }
            else continue;
            if (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline)
                return fail(LoadCode::Unsupported, "clip '" + c.name + "' uses CUBICSPLINE interpolation, which the pose evaluator does not implement");
            k.times.resize(ch.sampler->input->count);
            if (cgltf_accessor_unpack_floats(ch.sampler->input, k.times.data(), k.times.size()) != k.times.size())
                return fail(LoadCode::Malformed, "clip '" + c.name + "' keyframe times could not be read");
            for (size_t i = 0; i < k.times.size(); ++i)
                if (!std::isfinite(k.times[i]) || k.times[i] < 0 || (i && k.times[i] <= k.times[i - 1]))
                    return fail(LoadCode::Malformed, "clip '" + c.name + "' keyframe times must be finite, not negative and strictly increasing");
            k.step = ch.sampler->interpolation == cgltf_interpolation_type_step;
            k.values.resize(ch.sampler->output->count * (size_t)comps);
            if (cgltf_accessor_unpack_floats(ch.sampler->output, k.values.data(), k.values.size()) != k.values.size())
                return fail(LoadCode::Malformed, "clip '" + c.name + "' keyframe values could not be read");
            if (!std::all_of(k.values.begin(), k.values.end(), [](float v) { return std::isfinite(v); }))
                return fail(LoadCode::Malformed, "clip '" + c.name + "' keyframe values must be finite");
            if (!k.times.empty()) c.duration = std::max(c.duration, k.times.back());
            c.channels.push_back(std::move(k));
        }
        out.clips.push_back(std::move(c));
    }
    owner.reset();
    {
        static const char* const kinds[5] = { "unknown-mode", "POINTS", "LINES", "LINE_LOOP", "LINE_STRIP" };
        for (int k = 0; k < 5; ++k)
            if (skippedPrimitives[k]) out.warnings += (out.warnings.empty() ? "primitives not drawn (only triangles, strips and fans are loaded): " : ", ") + std::to_string(skippedPrimitives[k]) + " " + kinds[k];
    }
    if (out.verts.empty() || out.indices.empty()) return fail(LoadCode::NoGeometry, "no textured triangle geometry (needs triangle primitives with POSITION and TEXCOORD_0)" + (out.warnings.empty() ? std::string() : "; " + out.warnings));

    if (atlasUv) {
        LoadError atlasError = CheckAtlas(out, *atlasUv, label);
        if (atlasError.failed()) { SkinnedModel empty; empty.source = out.source; empty.name = out.name; out = std::move(empty); return atlasError; }
        WriteAtlas(out, *atlasUv);
    }
    GenerateTangents(out);
    for (size_t v = 0; v < out.verts.size(); ++v) {
        const SkinVertex& sv = out.verts[v];
        const float all[] = { sv.px, sv.py, sv.pz, sv.nx, sv.ny, sv.nz, sv.tx, sv.ty, sv.tz, sv.tw, sv.u, sv.v, sv.w[0], sv.w[1], sv.w[2], sv.w[3] };
        for (float f : all)
            if (!std::isfinite(f)) return fail(LoadCode::Malformed, "vertex " + std::to_string(v) + " is not finite after the node transforms and tangent generation");
    }
    out.loaded = true;
    return LoadError();
}

}

LoadError loadSkinnedFromMemory(ByteView glb, const ByteView* atlasUv, const std::string& label, SkinnedModel& out) {
    try { return LoadSkinnedImpl(glb, atlasUv, label, out); }
    catch (const std::bad_alloc&) { out = SkinnedModel(); return makeLoadError(LoadCode::OutOfMemory, label, "not enough memory to load the model"); }
    catch (const std::exception& e) { out = SkinnedModel(); return makeLoadError(LoadCode::Internal, label, std::string("unexpected exception while loading the model: ") + e.what()); }
    catch (...) { out = SkinnedModel(); return makeLoadError(LoadCode::Internal, label, "unexpected exception while loading the model"); }
}

LoadError applyAtlasUv(SkinnedModel& model, ByteView atlasUv, const std::string& label) {
    try {
        if (!model.loaded) return makeLoadError(LoadCode::InvalidArgument, label, "model is not loaded");
        LoadError e = CheckAtlas(model, atlasUv, label);
        if (e.failed()) return e;
        WriteAtlas(model, atlasUv);
        GenerateTangents(model);
        return LoadError();
    }
    catch (const std::bad_alloc&) { return makeLoadError(LoadCode::OutOfMemory, label, "not enough memory to apply the atlas UVs"); }
    catch (...) { return makeLoadError(LoadCode::Internal, label, "unexpected exception while applying the atlas UVs"); }
}

bool loadSkinned(const std::string& glbPath, const std::string& atlasUvBin, SkinnedModel& out, std::string& err) {
    out = SkinnedModel();
    err.clear();
    try {
    std::vector<uint8_t> glb, atlas;
    LoadError e = readWholeFile(glbPath, glb);
    if (e.failed()) { err = e.message; return false; }
    std::string warning;
    bool haveAtlas = false;
    if (!atlasUvBin.empty()) {
        if (readWholeFile(atlasUvBin, atlas).ok()) haveAtlas = true;
        else warning = "atlas uv bin not found: " + atlasUvBin + "; keeping glb UVs";
    }
    const ByteView atlasView(atlas);
    e = loadSkinnedFromMemory(ByteView(glb), haveAtlas ? &atlasView : nullptr, glbPath, out);
    if (e.failed() && haveAtlas && (e.code == LoadCode::AtlasSizeMismatch || e.code == LoadCode::AtlasNotFinite)) {
        warning = e.message + "; keeping glb UVs";
        e = loadSkinnedFromMemory(ByteView(glb), nullptr, glbPath, out);
    }
    if (e.failed()) { err = e.message; return false; }
    err = warning;
    if (!out.warnings.empty()) err += (err.empty() ? "" : "; ") + out.warnings;
    return true;
    } catch (...) { out = SkinnedModel(); err = glbPath + ": unexpected exception while loading the model"; return false; }
}

// ---- pose evaluation ----
static void SampleChannel(const Channel& ch, float t, float* out, int comps) {
    const size_t n = ch.times.size();
    if (n == 0) return;
    if (t <= ch.times.front() || n == 1) { std::memcpy(out, ch.values.data(), sizeof(float) * (size_t)comps); return; }
    if (t >= ch.times.back()) { std::memcpy(out, ch.values.data() + (n - 1) * (size_t)comps, sizeof(float) * (size_t)comps); return; }
    size_t hi = (size_t)(std::upper_bound(ch.times.begin(), ch.times.end(), t) - ch.times.begin());
    size_t lo = hi - 1;
    if (ch.step) { std::memcpy(out, ch.values.data() + lo * (size_t)comps, sizeof(float) * (size_t)comps); return; }
    float span = ch.times[hi] - ch.times[lo];
    float a = span > 0 ? (t - ch.times[lo]) / span : 0.0f;
    const float* v0 = ch.values.data() + lo * (size_t)comps; const float* v1 = ch.values.data() + hi * (size_t)comps;
    if (comps == 4) {
        XMVECTOR q = XMQuaternionSlerp(XMVectorSet(v0[0], v0[1], v0[2], v0[3]), XMVectorSet(v1[0], v1[1], v1[2], v1[3]), a);
        XMFLOAT4 qf; XMStoreFloat4(&qf, q); out[0] = qf.x; out[1] = qf.y; out[2] = qf.z; out[3] = qf.w;
    } else for (int i = 0; i < comps; ++i) out[i] = v0[i] + (v1[i] - v0[i]) * a;
}

struct LocalTRS { XMFLOAT3 t; XMFLOAT4 r; XMFLOAT3 s; };

static void EvalClip(const SkinnedModel& m, int clip, float t, std::vector<LocalTRS>& local) {
    for (size_t j = 0; j < m.joints.size(); ++j) local[j] = { m.joints[j].t, m.joints[j].r, m.joints[j].s };
    if (clip < 0 || clip >= (int)m.clips.size()) return;
    const Clip& c = m.clips[(size_t)clip];
    if (c.duration > 0) t = c.loop ? std::fmod(std::max(0.0f, t), c.duration) : std::min(std::max(0.0f, t), c.duration);
    for (const Channel& ch : c.channels) {
        LocalTRS& l = local[(size_t)ch.joint];
        if (ch.path == 0) { float v[3]; SampleChannel(ch, t, v, 3); l.t = XMFLOAT3(v[0], v[1], v[2]); }
        else if (ch.path == 1) { float v[4]; SampleChannel(ch, t, v, 4); l.r = XMFLOAT4(v[0], v[1], v[2], v[3]); }
        else { float v[3]; SampleChannel(ch, t, v, 3); l.s = XMFLOAT3(v[0], v[1], v[2]); }
    }
}

void evalPose(const SkinnedModel& m, int clipA, float ta, int clipB, float tb, float blend,
              XMFLOAT4X4* palette, XMFLOAT4X4* global) {
    const size_t nj = m.joints.size();
    if (nj == 0) return;
    static thread_local std::vector<LocalTRS> la, lb;
    static thread_local std::vector<XMFLOAT4X4> gl;
    la.resize(nj); lb.resize(nj); gl.resize(nj);
    EvalClip(m, clipA, ta, la);
    const bool mix = blend > 0.001f && clipB >= 0;
    if (mix) EvalClip(m, clipB, tb, lb);
    for (size_t j = 0; j < nj; ++j) {
        XMVECTOR t = XMLoadFloat3(&la[j].t), r = XMLoadFloat4(&la[j].r), s = XMLoadFloat3(&la[j].s);
        if (mix) {
            t = XMVectorLerp(t, XMLoadFloat3(&lb[j].t), blend);
            r = XMQuaternionSlerp(r, XMLoadFloat4(&lb[j].r), blend);
            s = XMVectorLerp(s, XMLoadFloat3(&lb[j].s), blend);
        }
        XMMATRIX L = XMMatrixMultiply(XMMatrixMultiply(XMMatrixScalingFromVector(s), XMMatrixRotationQuaternion(r)), XMMatrixTranslationFromVector(t));
        XMMATRIX Lb = XMMatrixMultiply(L, XMLoadFloat4x4(&m.joints[j].base));
        XMMATRIX G = m.joints[j].parent < 0 ? Lb : XMMatrixMultiply(Lb, XMLoadFloat4x4(&gl[(size_t)m.joints[j].parent]));
        XMStoreFloat4x4(&gl[j], G);
        if (global) global[j] = gl[j];
        if (palette) XMStoreFloat4x4(&palette[j], XMMatrixMultiply(XMLoadFloat4x4(&m.joints[j].ibm), G));
    }
}

} // namespace model
