// DirectX 11 renderer implementation.
#include "m2rig/renderer.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <wrl/client.h>

#include "m2rig/logging.hpp"
#include "m2rig/mesh.hpp"

namespace m2rig {

using Microsoft::WRL::ComPtr;

namespace {

const char* kShaderSrc = R"(cbuffer Frame : register(b0) { float4x4 gWvp; };
Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);
struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float4 col : COLOR; float2 uv : TEXCOORD; };
struct VsOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; float4 col : COLOR; float2 uv : TEXCOORD; };
VsOut VsMain(VsIn i) { VsOut o; o.pos = mul(float4(i.pos, 1.0f), gWvp); o.nrm = i.nrm; o.col = i.col; o.uv = i.uv; return o; }
float4 PsMain(VsOut i) : SV_TARGET {
  float3 L = normalize(float3(0.4f, 0.8f, 0.45f));
  float d = saturate(dot(normalize(i.nrm), L)) * 0.65f + 0.35f;
  return float4(i.col.rgb * d, i.col.a);
}
float4 PsFlat(VsOut i) : SV_TARGET { return i.col; }
float4 PsTextured(VsOut i) : SV_TARGET {
  float3 L = normalize(float3(0.4f, 0.8f, 0.45f));
  float d = saturate(dot(normalize(i.nrm), L)) * 0.65f + 0.35f;
  float4 t = gTex.Sample(gSamp, i.uv);
  return float4(t.rgb * i.col.rgb * d, i.col.a);
}
)";

bool compileShader(const char* src, const char* entry, const char* target, ComPtr<ID3DBlob>& out,
                   std::string& err) {
    ComPtr<ID3DBlob> errors;
    const HRESULT hr =
        D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target,
                   D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &errors);
    if (FAILED(hr)) {
        err = std::string("D3DCompile(") + entry + ") failed";
        if (errors) err += std::string(": ") + static_cast<const char*>(errors->GetBufferPointer());
        return false;
    }
    return true;
}

}  // namespace

struct Renderer::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> psLit;
    ComPtr<ID3D11PixelShader> psFlat;
    ComPtr<ID3D11PixelShader> psTex;
    ComPtr<ID3D11SamplerState> sampler;
    std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> textures;
    std::string activeTexture;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> frameCb;
    ComPtr<ID3D11Buffer> lineVb;
    ComPtr<ID3D11RasterizerState> rsSolid;
    ComPtr<ID3D11RasterizerState> rsWire;
    ComPtr<ID3D11RasterizerState> rsWireBias;
    ComPtr<ID3D11DepthStencilState> dsState;
    ComPtr<ID3D11DepthStencilState> dsNoDepth;
    std::size_t lineVbCap = 0;

    struct MeshBuffers {
        ComPtr<ID3D11Buffer> vb;
        ComPtr<ID3D11Buffer> ib;
        UINT indexCount = 0;
    };
    std::unordered_map<std::string, MeshBuffers> meshes;

    bool createTarget(int w, int h) {
        rtv.Reset();
        dsv.Reset();
        if (FAILED(swapChain->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h),
                                            DXGI_FORMAT_UNKNOWN, 0)))
            return false;
        ComPtr<ID3D11Texture2D> back;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
        if (FAILED(device->CreateRenderTargetView(back.Get(), nullptr, &rtv))) return false;
        D3D11_TEXTURE2D_DESC dd{};
        back->GetDesc(&dd);
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        ComPtr<ID3D11Texture2D> depth;
        if (FAILED(device->CreateTexture2D(&dd, nullptr, &depth))) return false;
        if (FAILED(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv))) return false;
        return true;
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() { shutdown(); }

bool Renderer::init(HWND hwnd, int width, int height, std::string& outError) {
    if (initialized_) return true;
    Impl& I = *impl_;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = static_cast<UINT>(width);
    sd.BufferDesc.Height = static_cast<UINT>(height);
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    static const D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                                D3D_FEATURE_LEVEL_10_1};
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, kLevels, 3, D3D11_SDK_VERSION, &sd,
        &I.swapChain, &I.device, &obtained, &I.context);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, kLevels,
                                           3, D3D11_SDK_VERSION, &sd, &I.swapChain, &I.device,
                                           &obtained, &I.context);
    }
    if (FAILED(hr)) {
        outError = "D3D11 device creation failed (HRESULT " + std::to_string(hr) + ").";
        return false;
    }
    if (!I.createTarget(width, height)) {
        outError = "Failed to create render target views.";
        return false;
    }

    ComPtr<ID3DBlob> vsBlob;
    if (!compileShader(kShaderSrc, "VsMain", "vs_5_0", vsBlob, outError)) return false;
    if (FAILED(I.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                            nullptr, &I.vs))) {
        outError = "CreateVertexShader failed.";
        return false;
    }
    ComPtr<ID3DBlob> psBlob;
    if (!compileShader(kShaderSrc, "PsMain", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                           nullptr, &I.psLit))) {
        outError = "CreatePixelShader(lit) failed.";
        return false;
    }
    if (!compileShader(kShaderSrc, "PsFlat", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                            nullptr, &I.psFlat))) {
        outError = "CreatePixelShader(flat) failed.";
        return false;
    }
    if (!compileShader(kShaderSrc, "PsTextured", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                            nullptr, &I.psTex))) {
        outError = "CreatePixelShader(textured) failed.";
        return false;
    }
    D3D11_SAMPLER_DESC samp{};
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samp.MaxAnisotropy = 1;
    samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp.MinLOD = 0.0f;
    samp.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(I.device->CreateSamplerState(&samp, &I.sampler))) {
        outError = "CreateSamplerState failed.";
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(I.device->CreateInputLayout(elems, 4, vsBlob->GetBufferPointer(),
                                            vsBlob->GetBufferSize(), &I.layout))) {
        outError = "CreateInputLayout failed.";
        return false;
    }

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(float) * 16;
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(I.device->CreateBuffer(&cb, nullptr, &I.frameCb))) {
        outError = "Failed to create frame constant buffer.";
        return false;
    }

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_BACK;
    rs.DepthClipEnable = TRUE;
    if (FAILED(I.device->CreateRasterizerState(&rs, &I.rsSolid))) {
        outError = "Failed to create solid rasterizer state.";
        return false;
    }
    rs.FillMode = D3D11_FILL_WIREFRAME;
    rs.CullMode = D3D11_CULL_NONE;
    if (FAILED(I.device->CreateRasterizerState(&rs, &I.rsWire))) {
        outError = "Failed to create wireframe rasterizer state.";
        return false;
    }
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(I.device->CreateDepthStencilState(&ds, &I.dsState))) {
        outError = "Failed to create depth stencil state.";
        return false;
    }
    D3D11_DEPTH_STENCIL_DESC dsNo{};
    dsNo.DepthEnable = FALSE;
    dsNo.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsNo.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(I.device->CreateDepthStencilState(&dsNo, &I.dsNoDepth))) {
        outError = "Failed to create no-depth stencil state.";
        return false;
    }
    D3D11_RASTERIZER_DESC rsBias{};
    rsBias.FillMode = D3D11_FILL_WIREFRAME;
    rsBias.CullMode = D3D11_CULL_NONE;
    rsBias.DepthClipEnable = TRUE;
    rsBias.DepthBias = 20;
    rsBias.SlopeScaledDepthBias = 1.0f;
    if (FAILED(I.device->CreateRasterizerState(&rsBias, &I.rsWireBias))) {
        outError = "Failed to create wire overlay rasterizer state.";
        return false;
    }

    initialized_ = true;
    Logger::instance().info("D3D11 renderer initialized (feature level " +
                            std::to_string((obtained >> 12) & 0xF) + "." +
                            std::to_string((obtained >> 8) & 0xF) + ").");
    return true;
}

void Renderer::shutdown() {
    if (!initialized_) return;
    impl_->meshes.clear();
    impl_->lineVb.Reset();
    impl_->frameCb.Reset();
    impl_->layout.Reset();
    impl_->vs.Reset();
    impl_->psLit.Reset();
    impl_->psFlat.Reset();
    impl_->psTex.Reset();
    impl_->sampler.Reset();
    impl_->textures.clear();
    impl_->activeTexture.clear();
    impl_->rsSolid.Reset();
    impl_->rsWire.Reset();
    impl_->rsWireBias.Reset();
    impl_->dsState.Reset();
    impl_->dsNoDepth.Reset();
    impl_->dsv.Reset();
    impl_->rtv.Reset();
    impl_->swapChain.Reset();
    impl_->context.Reset();
    impl_->device.Reset();
    initialized_ = false;
}

bool Renderer::resize(int width, int height) {
    if (!initialized_ || width <= 0 || height <= 0) return false;
    impl_->context->OMSetRenderTargets(0, nullptr, nullptr);
    return impl_->createTarget(width, height);
}

void Renderer::beginScenePass(int x, int y, int w, int h, const float clearColor[4]) {
    Impl& I = *impl_;
    // Full-window clear happens once per frame by the caller convention: the
    // first (and usually only) scene pass clears; guard tiny rects.
    if (w <= 0 || h <= 0) return;
    I.context->OMSetRenderTargets(1, I.rtv.GetAddressOf(), I.dsv.Get());
    I.context->OMSetDepthStencilState(I.dsState.Get(), 0);
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = static_cast<float>(x);
    vp.TopLeftY = static_cast<float>(y);
    vp.Width = static_cast<float>(w);
    vp.Height = static_cast<float>(h);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    I.context->RSSetViewports(1, &vp);
    // Scissor not strictly needed for a single viewport panel.
    I.context->ClearRenderTargetView(I.rtv.Get(), clearColor);
    I.context->ClearDepthStencilView(I.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    I.context->IASetInputLayout(I.layout.Get());
    I.context->VSSetShader(I.vs.Get(), nullptr, 0);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
    I.context->VSSetConstantBuffers(0, 1, I.frameCb.GetAddressOf());
}

void Renderer::endScenePass() {}

bool Renderer::uploadMesh(const std::string& key, const std::vector<GpuVertex>& vertices,
                          const std::vector<std::uint32_t>& indices, std::string& outError) {
    Impl& I = *impl_;
    if (!initialized_) {
        outError = "Renderer is not initialized.";
        return false;
    }
    if (vertices.empty() || indices.empty()) {
        outError = "Cannot upload empty mesh '" + key + "'.";
        return false;
    }
    Impl::MeshBuffers mb;
    D3D11_BUFFER_DESC vd{};
    vd.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GpuVertex));
    vd.Usage = D3D11_USAGE_DEFAULT;
    vd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsd{};
    vsd.pSysMem = vertices.data();
    if (FAILED(I.device->CreateBuffer(&vd, &vsd, &mb.vb))) {
        outError = "Failed to create vertex buffer for '" + key + "'.";
        return false;
    }
    D3D11_BUFFER_DESC id{};
    id.ByteWidth = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
    id.Usage = D3D11_USAGE_DEFAULT;
    id.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA isd{};
    isd.pSysMem = indices.data();
    if (FAILED(I.device->CreateBuffer(&id, &isd, &mb.ib))) {
        outError = "Failed to create index buffer for '" + key + "'.";
        return false;
    }
    mb.indexCount = static_cast<UINT>(indices.size());
    I.meshes[key] = std::move(mb);
    return true;
}

void Renderer::releaseMesh(const std::string& key) { impl_->meshes.erase(key); }

bool Renderer::setTexture(const std::string& key, const std::uint8_t* rgba, std::uint32_t width,
                          std::uint32_t height, std::string& outError) {
    Impl& I = *impl_;
    if (!initialized_) {
        outError = "Renderer is not initialized.";
        return false;
    }
    if (!rgba || width == 0 || height == 0 || width > 16384 || height > 16384) {
        outError = "Bad texture image for '" + key + "'.";
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba;
    init.SysMemPitch = width * 4;
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(I.device->CreateTexture2D(&td, &init, &tex))) {
        outError = "Failed to create texture for '" + key + "'.";
        return false;
    }
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(I.device->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
        outError = "Failed to create texture view for '" + key + "'.";
        return false;
    }
    I.textures[key] = std::move(srv);
    return true;
}

void Renderer::setActiveTexture(const std::string& key) { impl_->activeTexture = key; }

void Renderer::releaseTexture(const std::string& key) {
    impl_->textures.erase(key);
    if (impl_->activeTexture == key) impl_->activeTexture.clear();
}

void Renderer::drawMeshTextured(const std::string& key, const Mat4& worldViewProj, FillMode fill) {
    Impl& I = *impl_;
    auto tit = I.textures.find(I.activeTexture);
    if (tit == I.textures.end() || !tit->second) {
        drawMesh(key, worldViewProj, fill);
        return;
    }
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(I.psTex.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = tit->second.Get();
    I.context->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* samp = I.sampler.Get();
    I.context->PSSetSamplers(0, 1, &samp);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(it->second.indexCount, 0, 0);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    I.context->PSSetShaderResources(0, 1, &nullSrv);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawMesh(const std::string& key, const Mat4& worldViewProj, FillMode fill) {
    Impl& I = *impl_;
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        // HLSL default is column-major: transpose our row-major matrix.
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(it->second.indexCount, 0, 0);
}

void Renderer::drawLines(const std::vector<GpuVertex>& segments, const Mat4& worldViewProj) {
    Impl& I = *impl_;
    if (segments.empty()) return;
    if (segments.size() > I.lineVbCap) {
        I.lineVb.Reset();
        D3D11_BUFFER_DESC vd{};
        vd.ByteWidth = static_cast<UINT>(segments.size() * sizeof(GpuVertex));
        vd.Usage = D3D11_USAGE_DYNAMIC;
        vd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(I.device->CreateBuffer(&vd, nullptr, &I.lineVb))) return;
        I.lineVbCap = segments.size();
    }
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(I.context->Map(I.lineVb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    memcpy(map.pData, segments.data(), segments.size() * sizeof(GpuVertex));
    I.context->Unmap(I.lineVb.Get(), 0);
    D3D11_MAPPED_SUBRESOURCE cmap{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cmap))) {
        float* dst = static_cast<float*>(cmap.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(I.rsWire.Get());
    I.context->PSSetShader(I.psFlat.Get(), nullptr, 0);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, I.lineVb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    I.context->Draw(static_cast<UINT>(segments.size()), 0);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawMeshWireOverlay(const std::string& key, const Mat4& worldViewProj) {
    Impl& I = *impl_;
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    // Depth-biased wireframe so the overlay never z-fights the solid pass.
    I.context->RSSetState(I.rsWireBias.Get());
    I.context->PSSetShader(I.psFlat.Get(), nullptr, 0);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(it->second.indexCount, 0, 0);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawLinesXRay(const std::vector<GpuVertex>& segments,
                             const Mat4& worldViewProj) {
    Impl& I = *impl_;
    if (segments.empty()) return;
    I.context->OMSetDepthStencilState(I.dsNoDepth.Get(), 0);
    drawLines(segments, worldViewProj);
    I.context->OMSetDepthStencilState(I.dsState.Get(), 0);
}

void Renderer::present(bool vsync) { impl_->swapChain->Present(vsync ? 1 : 0, 0); }

void* Renderer::deviceForBackend() { return impl_->device.Get(); }
void* Renderer::contextForBackend() { return impl_->context.Get(); }

}  // namespace m2rig
