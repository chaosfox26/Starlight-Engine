// SampleGame DX12 client — "off" backend: bilinear compute upscale, no vendor SDK.
// This is the floor of "playable on any system": every quality mode renders at the SDK-standard
// ratio and gets filtered to the output size by an 8x8 compute kernel that exists on any D3D12
// adapter. It also serves as the reference the vendor paths are compared against in the results
// file (same render size, same scene, different reconstruction).
#include "upscale.h"
#include <d3dcompiler.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace up {

namespace {

const char* kCS =
    "Texture2D<float4>   src : register(t0);\n"
    "RWTexture2D<float4> dst : register(u0);\n"
    "SamplerState        smp : register(s0);\n"
    "cbuffer P : register(b0) { float2 invOut; uint2 outSize; }\n"
    "[numthreads(8,8,1)]\n"
    "void CSMain(uint3 id : SV_DispatchThreadID) {\n"
    "  if (id.x >= outSize.x || id.y >= outSize.y) return;\n"
    "  float2 uv = (float2(id.xy) + 0.5) * invOut;\n"
    "  dst[id.xy] = float4(src.SampleLevel(smp, uv, 0).rgb, 1.0);\n"
    "}\n";

struct OffUpscaler : Upscaler {
    ComPtr<ID3D12Device>         dev;
    ComPtr<ID3D12RootSignature>  rs;
    ComPtr<ID3D12PipelineState>  pso;
    ComPtr<ID3D12DescriptorHeap> heap;      // 2 shader-visible: SRV color, UAV output
    ID3D12Resource* lastSrc = nullptr;
    ID3D12Resource* lastDst = nullptr;
    UINT   inc = 0, outW = 0, outH = 0;
    int    curMode = 0;
    LogFn  log = nullptr;

    Path path() const override { return Path::Off; }

    bool init(const DeviceCtx& ctx, ComPtr<IDXGISwapChain4>& swap) override {
        (void)swap;
        dev = ctx.device; outW = ctx.outW; outH = ctx.outH; log = ctx.log;
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 1; ranges[0].BaseShaderRegister = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 1; ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;
        D3D12_ROOT_PARAMETER rp[2]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[0].DescriptorTable.NumDescriptorRanges = 2; rp[0].DescriptorTable.pDescriptorRanges = ranges;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[1].Constants.ShaderRegister = 0; rp[1].Constants.Num32BitValues = 4;
        D3D12_STATIC_SAMPLER_DESC ss{};
        ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss.ShaderRegister = 0; ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsd{}; rsd.NumParameters = 2; rsd.pParameters = rp;
        rsd.NumStaticSamplers = 1; rsd.pStaticSamplers = &ss;
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) { if (log) log("off: root sig failed"); return false; }
        if (FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rs)))) return false;
        ComPtr<ID3DBlob> cs, cerr;
        if (FAILED(D3DCompile(kCS, strlen(kCS), "off_cs", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &cs, &cerr))) {
            if (log) log(std::string("off: CS compile failed: ") + (cerr ? (const char*)cerr->GetBufferPointer() : "?"));
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = rs.Get();
        pd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.NumDescriptors = 2; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) return false;
        inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (log) log("off: bilinear compute upscaler ready");
        return true;
    }
    void shutdown() override { heap.Reset(); pso.Reset(); rs.Reset(); dev.Reset(); lastSrc = lastDst = nullptr; }

    bool resizeOutput(UINT width, UINT height) override { outW=width; outH=height; lastSrc=lastDst=nullptr; return true; }

    int modeCount() const override { return kStdModeCount; }
    const char* modeName(int i) const override { return (i >= 0 && i < kStdModeCount) ? kStdModes[i].name : "?"; }
    int mode() const override { return curMode; }
    bool wantsJitter() const override { return false; }
    bool setMode(int i, UINT& rw, UINT& rh) override {
        if (i < 0 || i >= kStdModeCount) return false;
        curMode = i;
        rw = (UINT)((float)outW / kStdModes[i].ratio + 0.5f); if (rw < 16) rw = 16;
        rh = (UINT)((float)outH / kStdModes[i].ratio + 0.5f); if (rh < 16) rh = 16;
        return true;
    }

    bool fgSupported() const override { return false; }
    bool fgEnabled() const override { return false; }
    void setFG(bool) override {}
    std::string fgStatus() const override { return "FG n/a"; }

    void evaluate(const FrameIn& f) override {
        if (f.color != lastSrc || f.output != lastDst) {
            D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
            D3D12_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
            sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            dev->CreateShaderResourceView(f.color, &sv, h);
            h.ptr += inc;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uv{}; uv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            dev->CreateUnorderedAccessView(f.output, nullptr, &uv, h);
            lastSrc = f.color; lastDst = f.output;
        }
        ID3D12DescriptorHeap* heaps[] = { heap.Get() };
        f.cmd->SetDescriptorHeaps(1, heaps);
        f.cmd->SetComputeRootSignature(rs.Get());
        f.cmd->SetPipelineState(pso.Get());
        f.cmd->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
        struct { float ix, iy; UINT w, h; } c{ 1.0f / (float)f.outW, 1.0f / (float)f.outH, f.outW, f.outH };
        f.cmd->SetComputeRoot32BitConstants(1, 4, &c, 0);
        f.cmd->Dispatch((f.outW + 7) / 8, (f.outH + 7) / 8, 1);
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = f.output;
        f.cmd->ResourceBarrier(1, &b);
    }

    std::string versionString() const override { return "bilinear compute (no SDK)"; }
};

} // namespace

Upscaler* createOff() { return new OffUpscaler(); }

} // namespace up
