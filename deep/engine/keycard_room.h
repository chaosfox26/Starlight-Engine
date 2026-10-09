#pragma once
#include "render_device.h"
#include "keycard_view.h"
#include "upscale/upscale_paths.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace keycard_room {

inline std::string moduleFolder(const void* address) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &module);
    std::wstring path = up::modulePath(module);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), nullptr, 0, nullptr, nullptr);
    std::string narrow((size_t)std::max(0, bytes), '\0');
    if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), narrow.data(), bytes, nullptr, nullptr);
    return narrow;
}

struct Part {
    GpuMesh mesh;
    XMMATRIX world = XMMatrixIdentity();
    XMFLOAT3 color{};
    float emissive = 0, modulator = 0;
};

inline std::vector<Part> build(RenderDevice& device, const keycard::KeyCard& card) {
    std::vector<Part> parts;
    std::map<int, GpuMesh> rings;
    for (const keycard::Part& p : card.parts) {
        Part part;
        XMFLOAT3 origin(p.origin[0], p.origin[1], p.origin[2]);
        if (p.shape == "cube") {
            part.mesh = device.boxMesh;
            origin.x += p.axisY[0] * 0.5f; origin.y += p.axisY[1] * 0.5f; origin.z += p.axisY[2] * 0.5f;
        } else if (p.shape == "cylinder") {
            part.mesh = device.cylMesh;
        } else {
            const int key = (int)lroundf(p.ringInner * 10000.0f);
            if (key == 9600) part.mesh = device.ringThinMesh;
            else if (key == 8200) part.mesh = device.ringMesh;
            else {
                auto it = rings.find(key);
                if (it == rings.end()) { Mesh m; BuildRing(m, 96, p.ringInner); it = rings.emplace(key, device.uploadMesh(m)).first; }
                part.mesh = it->second;
            }
        }
        part.world = XMMatrixSet(p.axisX[0], p.axisX[1], p.axisX[2], 0.0f, p.axisY[0], p.axisY[1], p.axisY[2], 0.0f,
                                 p.axisZ[0], p.axisZ[1], p.axisZ[2], 0.0f, origin.x, origin.y, origin.z, 1.0f);
        part.color = XMFLOAT3(p.color.r, p.color.g, p.color.b);
        part.emissive = p.glow;
        if (!p.group.empty()) part.modulator = card.lighting.modulator("Darkness", "group:" + p.group);
        parts.push_back(std::move(part));
    }
    return parts;
}

inline void draw(RenderDevice& device, const std::vector<Part>& parts, float darkness) {
    for (const Part& p : parts) {
        const float k = p.modulator != 0.0f ? std::max(0.0f, 1.0f + p.modulator * darkness) : 1.0f;
        device.drawMesh(p.mesh, p.world, XMFLOAT3(p.color.x * k, p.color.y * k, p.color.z * k), p.emissive);
    }
}

inline void applyCamera(RenderDevice& device, const keycard::KeyCard& card) {
    if (!card.view.present) return;
    device.hostCamera = true;
    device.hostFovDegrees = card.view.fovDegrees;
    device.hostNear = card.view.nearPlane;
    device.hostFar = card.view.farPlane;
    if (card.view.follow) {
        device.hostTarget = XMFLOAT3(0, card.view.targetHeight, 0);
        device.hostEye = XMFLOAT3(0, card.view.targetHeight + card.view.defaultHeight, card.view.defaultDistance);
    }
}

}
