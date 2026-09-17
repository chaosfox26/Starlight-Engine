// Starlight DX12 client — skinned character models (see skinned.h).
#include "skinned.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
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

bool loadSkinned(const std::string& glbPath, const std::string& atlasUvBin, SkinnedModel& out, std::string& err) {
    out = SkinnedModel();
    out.source = glbPath;
    size_t slash = glbPath.find_last_of("/\\");
    out.name = glbPath.substr(slash == std::string::npos ? 0 : slash + 1);
    if (out.name.size() > 4 && out.name.compare(out.name.size() - 4, 4, ".glb") == 0) out.name.resize(out.name.size() - 4);

    cgltf_options opt{};
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse_file(&opt, glbPath.c_str(), &data);
    if (r != cgltf_result_success) { err = "cgltf_parse_file failed (code " + std::to_string((int)r) + ") for " + glbPath; return false; }
    r = cgltf_load_buffers(&opt, data, glbPath.c_str());
    if (r != cgltf_result_success) { cgltf_free(data); err = "cgltf_load_buffers failed (code " + std::to_string((int)r) + ")"; return false; }

    // ---- skeleton: the first skin's joint list, in palette order ----
    std::unordered_map<const cgltf_node*, int> jointOf;
    const cgltf_skin* skin = data->skins_count ? &data->skins[0] : nullptr;
    if (skin) {
        for (cgltf_size j = 0; j < skin->joints_count; ++j) jointOf[skin->joints[j]] = (int)j;
        for (cgltf_size j = 0; j < skin->joints_count; ++j) {
            const cgltf_node* n = skin->joints[j];
            Joint jt; jt.name = n->name ? n->name : ("joint" + std::to_string(j));
            auto pit = n->parent ? jointOf.find(n->parent) : jointOf.end();
            jt.parent = pit == jointOf.end() ? -1 : pit->second;
            if (n->has_translation) jt.t = XMFLOAT3(n->translation[0], n->translation[1], n->translation[2]);
            if (n->has_rotation) jt.r = XMFLOAT4(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]);
            if (n->has_scale) jt.s = XMFLOAT3(n->scale[0], n->scale[1], n->scale[2]);
            float ibm[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            if (skin->inverse_bind_matrices) cgltf_accessor_read_float(skin->inverse_bind_matrices, j, ibm, 16);
            XMStoreFloat4x4(&jt.ibm, FromGltf(ibm));
            out.joints.push_back(jt);
        }
        // parents must precede children for the single-pass global evaluation
        for (size_t j = 0; j < out.joints.size(); ++j)
            if (out.joints[j].parent >= (int)j) { cgltf_free(data); err = "skin joint order is not parent-first in " + glbPath; return false; }
    }

    // ---- geometry: same walk as bake_job.py (scene roots, depth-first, node mesh before children) ----
    bool first = true;
    auto grow = [&](float x, float y, float z) {
        if (first) { out.bbMin = out.bbMax = XMFLOAT3(x, y, z); first = false; return; }
        out.bbMin.x = std::min(out.bbMin.x, x); out.bbMin.y = std::min(out.bbMin.y, y); out.bbMin.z = std::min(out.bbMin.z, z);
        out.bbMax.x = std::max(out.bbMax.x, x); out.bbMax.y = std::max(out.bbMax.y, y); out.bbMax.z = std::max(out.bbMax.z, z);
    };
    std::function<void(const cgltf_node*)> walk = [&](const cgltf_node* node) {
        if (node->mesh) {
            float world[16]; cgltf_node_transform_world(node, world);
            XMMATRIX W = node->skin ? XMMatrixIdentity() : FromGltf(world);
            XMMATRIX Wn = XMMatrixTranspose(XMMatrixInverse(nullptr, W));
            std::string nodeName = node->name ? node->name : "";
            for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
                const cgltf_primitive* prim = &node->mesh->primitives[pi];
                if (prim->type != cgltf_primitive_type_triangles) continue;
                const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *jn = nullptr, *wt = nullptr;
                for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                    const cgltf_attribute& at = prim->attributes[a];
                    if (at.type == cgltf_attribute_type_position) pos = at.data;
                    else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                    else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                    else if (at.type == cgltf_attribute_type_joints && at.index == 0) jn = at.data;
                    else if (at.type == cgltf_attribute_type_weights && at.index == 0) wt = at.data;
                }
                if (!pos || !uv) continue;   // bake_job skips these too (keeps the atlas stream aligned)
                Submesh sm; sm.node = nodeName;
                if (prim->material) {
                    sm.material = prim->material->name ? prim->material->name : "";
                    if (prim->material->has_pbr_metallic_roughness) {
                        const float* bc = prim->material->pbr_metallic_roughness.base_color_factor;
                        sm.baseColor = XMFLOAT3(bc[0], bc[1], bc[2]);
                    }
                    sm.emissive = sm.material == "glow" || sm.material == "catchlight";
                }
                // Node semantics are interpreted by the selected project.
                sm.firstIndex = (uint32_t)out.indices.size();
                uint32_t base = (uint32_t)out.verts.size();
                for (cgltf_size v = 0; v < pos->count; ++v) {
                    float p[3] = { 0,0,0 }, nn[3] = { 0,1,0 }, t[2] = { 0,0 }, w[4] = { 1,0,0,0 };
                    cgltf_uint j[4] = { 0,0,0,0 };
                    cgltf_accessor_read_float(pos, v, p, 3);
                    if (nrm) cgltf_accessor_read_float(nrm, v, nn, 3);
                    cgltf_accessor_read_float(uv, v, t, 2);
                    if (jn) cgltf_accessor_read_uint(jn, v, j, 4);
                    if (wt) cgltf_accessor_read_float(wt, v, w, 4);
                    XMFLOAT3 fp, fn;
                    XMStoreFloat3(&fp, XMVector3Transform(XMVectorSet(p[0], p[1], p[2], 1.0f), W));
                    XMStoreFloat3(&fn, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(nn[0], nn[1], nn[2], 0.0f), Wn)));
                    float ws = w[0] + w[1] + w[2] + w[3]; if (ws <= 0.0f) { ws = 1.0f; w[0] = 1.0f; }
                    SkinVertex sv{};
                    sv.px = fp.x; sv.py = fp.y; sv.pz = fp.z; sv.nx = fn.x; sv.ny = fn.y; sv.nz = fn.z;
                    sv.u = t[0]; sv.v = t[1];
                    for (int k = 0; k < 4; ++k) { sv.j[k] = (uint8_t)std::min<cgltf_uint>(j[k], 255u); sv.w[k] = w[k] / ws; }
                    out.verts.push_back(sv);
                    grow(fp.x, fp.y, fp.z);
                }
                if (prim->indices) for (cgltf_size i = 0; i < prim->indices->count; ++i) out.indices.push_back(base + (uint32_t)cgltf_accessor_read_index(prim->indices, i));
                else for (cgltf_size i = 0; i < pos->count; ++i) out.indices.push_back(base + (uint32_t)i);
                sm.indexCount = (uint32_t)out.indices.size() - sm.firstIndex;
                out.subs.push_back(sm);
            }
        }
        for (cgltf_size c = 0; c < node->children_count; ++c) walk(node->children[c]);
    };
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene) for (cgltf_size i = 0; i < scene->nodes_count; ++i) walk(scene->nodes[i]);
    else for (cgltf_size i = 0; i < data->nodes_count; ++i) if (!data->nodes[i].parent) walk(&data->nodes[i]);

    // ---- clips ----
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& an = data->animations[ai];
        Clip c; c.name = an.name ? an.name : ("clip" + std::to_string(ai)); c.loop = ClipLoops(c.name);
        for (cgltf_size ci = 0; ci < an.channels_count; ++ci) {
            const cgltf_animation_channel& ch = an.channels[ci];
            auto it = jointOf.find(ch.target_node);
            if (it == jointOf.end() || !ch.sampler || !ch.sampler->input || !ch.sampler->output) continue;
            Channel k; k.joint = it->second;
            int comps = 0;
            if (ch.target_path == cgltf_animation_path_type_translation) { k.path = 0; comps = 3; }
            else if (ch.target_path == cgltf_animation_path_type_rotation) { k.path = 1; comps = 4; }
            else if (ch.target_path == cgltf_animation_path_type_scale) { k.path = 2; comps = 3; }
            else continue;
            k.times.resize(ch.sampler->input->count);
            cgltf_accessor_unpack_floats(ch.sampler->input, k.times.data(), k.times.size());
            k.values.resize(ch.sampler->output->count * (size_t)comps);
            cgltf_accessor_unpack_floats(ch.sampler->output, k.values.data(), k.values.size());
            if (!k.times.empty()) c.duration = std::max(c.duration, k.times.back());
            c.channels.push_back(std::move(k));
        }
        out.clips.push_back(std::move(c));
    }
    cgltf_free(data);
    if (out.verts.empty() || out.indices.empty()) { err = "no textured triangle geometry in " + glbPath; return false; }

    // ---- atlas UV stream (bakes-v1): float32 [vertices][2] in the walk order above ----
    if (!atlasUvBin.empty()) {
        FILE* f = nullptr;
        if (fopen_s(&f, atlasUvBin.c_str(), "rb") == 0 && f) {
            std::vector<float> uvs(out.verts.size() * 2);
            size_t got = fread(uvs.data(), sizeof(float), uvs.size(), f);
            int extra = fgetc(f);
            fclose(f);
            if (got == uvs.size() && extra == EOF) {
                for (size_t i = 0; i < out.verts.size(); ++i) { out.verts[i].u = uvs[i * 2]; out.verts[i].v = uvs[i * 2 + 1]; }
                out.atlasUV = true;
            } else err = "atlas uv count mismatch for " + out.name + " (bin has " + std::to_string(got / 2 + (extra == EOF ? 0 : 1)) + "+ verts, glb walk " + std::to_string(out.verts.size()) + "); keeping glb UVs";
        } else err = "atlas uv bin not found: " + atlasUvBin + "; keeping glb UVs";
    }
    GenerateTangents(out);
    out.loaded = true;
    return true;
}

// ---- pose evaluation ----
static void SampleChannel(const Channel& ch, float t, float* out, int comps) {
    const size_t n = ch.times.size();
    if (n == 0) return;
    if (t <= ch.times.front() || n == 1) { std::memcpy(out, ch.values.data(), sizeof(float) * (size_t)comps); return; }
    if (t >= ch.times.back()) { std::memcpy(out, ch.values.data() + (n - 1) * (size_t)comps, sizeof(float) * (size_t)comps); return; }
    size_t hi = (size_t)(std::upper_bound(ch.times.begin(), ch.times.end(), t) - ch.times.begin());
    size_t lo = hi - 1;
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
        XMMATRIX G = m.joints[j].parent < 0 ? L : XMMatrixMultiply(L, XMLoadFloat4x4(&gl[(size_t)m.joints[j].parent]));
        XMStoreFloat4x4(&gl[j], G);
        if (global) global[j] = gl[j];
        if (palette) XMStoreFloat4x4(&palette[j], XMMatrixMultiply(XMLoadFloat4x4(&m.joints[j].ibm), G));
    }
}

} // namespace model
