// Starlight DX12 client — skinned character models.
// Loads a charfactory GLB (CONTRACT-3D v1.1: 29-joint rig, 27 clips, named mesh nodes
// body/hair/tattoo/gear:*), optionally remaps TEXCOORD_0 to the bakes-v1 atlas layout
// (<m>_atlas_uv.bin), generates tangents, and evaluates clip poses into a joint palette on the
// CPU (29 joints x a few dozen characters is far below one core's budget; the GPU skins).
#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>
#include "load_error.h"

namespace model {

struct SkinVertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;      // tangent + handedness
    float u, v;                // atlas UV when remapped, else the GLB's TEXCOORD_0
    uint8_t j[4];              // joint palette indices
    float w[4];                // weights (normalized)
};

struct Submesh {
    std::string node;          // GLB node name: body | hair | tattoo | gear:chest | gear:pants | gear:hip
    std::string material;      // GLB material name (fur, eye, glow, gear_cloth, ...)
    std::string gearSlot;      // "chest" for gear:chest etc., empty otherwise
    uint32_t firstIndex = 0, indexCount = 0;
    bool emissive = false;     // glow / catchlight materials
    DirectX::XMFLOAT3 baseColor{ 1, 1, 1 };   // material baseColorFactor (tint when no texture)
};

struct Joint {
    std::string name;
    int parent = -1;           // palette index of the parent joint, -1 = root
    DirectX::XMFLOAT3 t{ 0, 0, 0 };
    DirectX::XMFLOAT4 r{ 0, 0, 0, 1 };
    DirectX::XMFLOAT3 s{ 1, 1, 1 };
    DirectX::XMFLOAT4X4 ibm;   // inverse bind matrix (row-vector convention, like the rest of the client)
    DirectX::XMFLOAT4X4 base{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };   // product of the local transforms of the non-joint nodes between this joint and its parent joint (or the scene root); identity when the joint hangs directly off its parent
};

struct Channel {
    int joint = -1;
    int path = 0;              // 0 translation, 1 rotation, 2 scale
    bool step = false;         // glTF STEP interpolation: hold each key until the next
    std::vector<float> times;
    std::vector<float> values; // 3 floats per key (t/s) or 4 (r)
};

struct Clip {
    std::string name;
    float duration = 0;
    bool loop = true;
    std::vector<Channel> channels;
};

struct SkinnedModel {
    std::string name, source;
    std::vector<SkinVertex> verts;
    std::vector<uint32_t>   indices;
    std::vector<Submesh>    subs;
    std::vector<Joint>      joints;
    std::vector<Clip>       clips;
    DirectX::XMFLOAT3 bbMin{ 0, 0, 0 }, bbMax{ 0, 0, 0 };
    bool atlasUV = false;      // TEXCOORD_0 replaced by the atlas layout
    bool loaded = false;
    int clipIndex(const std::string& n) const;
    int jointIndex(const std::string& n) const;
};

// glbPath: the charfactory GLB. atlasUvBin: <m>_atlas_uv.bin (may be empty -> keep GLB UVs).
// Vertex order matches bake_job.py's walk (scene roots depth-first, triangle primitives with
// POSITION + TEXCOORD_0) so the atlas UV stream lines up 1:1; a count mismatch is reported.
bool loadSkinned(const std::string& glbPath, const std::string& atlasUvBin, SkinnedModel& out, std::string& err);

// glb: complete GLB bytes. atlasUv: nullptr = keep the GLB UVs; otherwise raw little-endian float32 [vertices][2], validated against the vertex count. The translation unit that owns cgltf must define CGLTF_IMPLEMENTATION exactly once per module.
LoadError loadSkinnedFromMemory(ByteView glb, const ByteView* atlasUv, const std::string& label, SkinnedModel& out);
LoadError applyAtlasUv(SkinnedModel& model, ByteView atlasUv, const std::string& label);

// Evaluate clip a at time ta blended toward clip b at tb (blend 0 = a only, 1 = b only).
// palette[j] = ibm[j] * global[j] (the skinning matrix), global[j] = model-space joint matrix
// (for attaching props to joints). Both arrays must hold joints.size() entries.
void evalPose(const SkinnedModel& m, int clipA, float ta, int clipB, float tb, float blend,
              DirectX::XMFLOAT4X4* palette, DirectX::XMFLOAT4X4* global);

} // namespace model
