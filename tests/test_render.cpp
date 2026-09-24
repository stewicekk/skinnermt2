// Headless D3D11 render smoke test (Windows only): hidden window, WARP
// fallback allowed, one frame through every draw path (solid, wire,
// textured + fallback, overlay, lines, X-ray). Proves the renderer
// initializes, shaders compile, the 72-byte vertex layout uploads, and the
// draws emit real pixels (backbuffer readback guards against a silently
// black viewport).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "../tests/expect.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "m2rig/dds.hpp"
#include "m2rig/ibl.hpp"
#include "m2rig/renderer.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skin_weights.hpp"

using namespace m2rig;

namespace {

LRESULT WINAPI testWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

M2RIG_TEST(render, headless_frame_through_all_paths) {
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigHeadlessTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (sample.succeeded()) {
        const Mesh& mesh = sample.value().mesh;
        CHECK_TRUE(renderer.uploadMesh("t", buildGpuVertices(mesh, MeshColoring::Solid),
                                       mesh.indices, err));
        const Mat4 vp = Mat4::lookAt({0, 2, 5}, {0, 1, 0}, {0, 1, 0}) *
                        Mat4::perspectiveFov(0.9f, 1.0f, 0.1f, 100.0f);
        const float clear[4] = {0.09f, 0.10f, 0.13f, 1.0f};
        auto countNonClear = [&](const std::vector<std::uint8_t>& pixels) {
            const int cr = static_cast<int>(clear[0] * 255.0f + 0.5f);
            const int cg = static_cast<int>(clear[1] * 255.0f + 0.5f);
            const int cb = static_cast<int>(clear[2] * 255.0f + 0.5f);
            int count = 0;
            for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
                const int dr = static_cast<int>(pixels[i + 0]) - cr;
                const int dg = static_cast<int>(pixels[i + 1]) - cg;
                const int db = static_cast<int>(pixels[i + 2]) - cb;
                if (dr * dr + dg * dg + db * db > 12 * 12 * 3) ++count;
            }
            return count;
        };
        // Model-only proof: grid and auxiliary lines cannot satisfy this.
        renderer.beginScenePass(-8, -8, 80, 80, clear);
        renderer.drawMesh("t", vp, FillMode::Solid);
        renderer.endScenePass();
        std::vector<std::uint8_t> modelPixels;
        int modelW = 0, modelH = 0;
        CHECK_TRUE(renderer.readBackbuffer(modelPixels, modelW, modelH));
        CHECK_EQ(modelW, 64);
        CHECK_EQ(modelH, 64);
        CHECK_EQ(modelPixels.size(), static_cast<std::size_t>(64 * 64 * 4));
        CHECK_TRUE(countNonClear(modelPixels) > 100);

        // Fresh pass for all remaining renderer paths.
        // Deliberately exercise the production boundary with a rect that is
        // partially outside the backbuffer (DPI/resize transitions can do
        // this for a docked ImGui panel). The renderer must clamp it.
        renderer.beginScenePass(-8, -8, 80, 80, clear);
        renderer.drawMesh("t", vp, FillMode::Solid);
        renderer.drawMesh("t", vp, FillMode::Wireframe);
        renderer.drawMeshWireOverlay("t", vp);
        // Textured path without a bound texture must fall back cleanly.
        renderer.setActiveTexture({});
        renderer.drawMeshTextured("t", vp, FillMode::Solid);
        // ... and with a real 4x4 red texture bound.
        std::vector<std::uint8_t> rgba(4 * 4 * 4, 0);
        for (std::size_t i = 0; i < 16; ++i) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 3] = 255;
        }
        CHECK_TRUE(renderer.setTexture("t", rgba.data(), 4, 4, err));
        renderer.setActiveTexture("t");
        renderer.drawMeshTextured("t", vp, FillMode::Solid);
        renderer.setActiveTexture({});
        renderer.releaseTexture("t");
        renderer.drawLines(buildGridLines(2.0f, 1.0f), vp);
        renderer.drawLinesXRay(buildGridLines(2.0f, 1.0f), vp);
        renderer.endScenePass();
        // Pixel proof: the frame must contain drawn content, not just clear.
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        if (!px.empty()) {
            CHECK_EQ(pw, 64);
            CHECK_EQ(ph, 64);
            const int nonClear = countNonClear(px);
            printf("    render pixels: %d non-clear of %d\n", nonClear, pw * ph);
            CHECK_TRUE(nonClear > 200);
        }
        renderer.present(false);
        renderer.releaseMesh("t");
    }
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

M2RIG_TEST(render, texture_srgb_view_and_flat_identity) {
    // Pins the Step-2 sRGB content: SRGB-vs-UNORM view selection plumbing
    // plus the unlit flat draw used by the Normals/Height/Weights/UV debug
    // modes (display passthrough must survive the linear pipeline exactly).
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigSrgbTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    // 1. View-format plumbing: default upload is sRGB (albedo), explicit
    // false is UNORM (data maps), unknown keys report false. Mid-grey 128
    // (not white: 255 is a decode fixpoint that would hide a broken decode).
    std::vector<std::uint8_t> grey(4 * 4 * 4, 128);
    CHECK_TRUE(renderer.setTexture("gS", grey.data(), 4, 4, err));
    CHECK_TRUE(renderer.textureIsSrgb("gS"));
    CHECK_TRUE(renderer.setTexture("gL", grey.data(), 4, 4, err, true, false));
    CHECK_TRUE(renderer.hasTexture("gL"));
    CHECK_FALSE(renderer.textureIsSrgb("gL"));
    CHECK_FALSE(renderer.textureIsSrgb("missing"));

    auto centerRgb = [](const std::vector<std::uint8_t>& p, int& r, int& g, int& b) {
        const std::size_t c = (static_cast<std::size_t>(32) * 64u + 32u) * 4u;
        r = static_cast<int>(p[c]);
        g = static_cast<int>(p[c + 1]);
        b = static_cast<int>(p[c + 2]);
    };
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    // 2. Flat identity: NDC-covering triangle, constant red, PsFlat must
    // pass it through to the backbuffer without the sRGB round-trip.
    std::vector<GpuVertex> tri(3);
    tri[0].position = {-1.0f, -1.0f, 0.5f};
    tri[1].position = {3.0f, -1.0f, 0.5f};
    tri[2].position = {-1.0f, 3.0f, 0.5f};
    for (auto& v : tri) {
        v.normal = {0.0f, 0.0f, 1.0f};
        v.color[0] = 1.0f;
        v.color[1] = 0.0f;
        v.color[2] = 0.0f;
        v.color[3] = 1.0f;
        v.uv[0] = 0.0f;
        v.uv[1] = 0.0f;
    }
    CHECK_TRUE(renderer.uploadMesh("f", tri, {0, 1, 2}, err));
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshFlat("f", Mat4::identity(), FillMode::Solid);
    renderer.endScenePass();
    {
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        CHECK_EQ(pw, 64);
        CHECK_EQ(ph, 64);
        if (!px.empty()) {
            int r = 0, g = 0, b = 0;
            centerRgb(px, r, g, b);
            printf("    flat center pixel: %d %d %d\n", r, g, b);
            CHECK_TRUE(r > 245 && g < 10 && b < 10);
        }
    }

    // 3. sRGB decode contrast + lit pipeline values: same mid-grey texel,
    // white vertex colors, PsTextured. The sRGB view decodes 128 -> ~0.216
    // linear (dark, ~78 display); the UNORM view keeps 0.502 (~116 display).
    // Hand-computed from the PsTextured math (nd~0.45, ambient + diffuse,
    // negligible spec); both would be IDENTICAL on the old gamma pipeline,
    // so this is the true regression pin for the decode.
    std::vector<GpuVertex> whiteTri = tri;
    for (auto& v : whiteTri) {
        v.color[0] = 1.0f;
        v.color[1] = 1.0f;
        v.color[2] = 1.0f;
    }
    CHECK_TRUE(renderer.uploadMesh("w", whiteTri, {0, 1, 2}, err));
    int sr = 0, sg = 0, sb = 0, lr = 0, lg = 0, lb = 0;
    renderer.setActiveTexture("gS");
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshTextured("w", Mat4::identity(), FillMode::Solid);
    renderer.endScenePass();
    {
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        if (!px.empty()) {
            centerRgb(px, sr, sg, sb);
            printf("    srgb128 center pixel: %d %d %d\n", sr, sg, sb);
            CHECK_TRUE(sr > 55 && sr < 105);
            CHECK_TRUE(abs(sr - sg) < 12 && abs(sr - sb) < 12);
        }
    }
    renderer.setActiveTexture("gL");
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshTextured("w", Mat4::identity(), FillMode::Solid);
    renderer.endScenePass();
    {
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        if (!px.empty()) {
            centerRgb(px, lr, lg, lb);
            printf("    linear128 center pixel: %d %d %d\n", lr, lg, lb);
            CHECK_TRUE(lr > 95 && lr < 140);
            CHECK_TRUE(abs(lr - lg) < 12 && abs(lr - lb) < 12);
            CHECK_TRUE(lr - sr > 15);  // decode contrast: same texel, darker via sRGB
        }
    }

    // 4. generateMips=false branch (MipLevels=1 view): must draw the same
    // pixels as the mipmapped upload for this solid texel.
    CHECK_TRUE(renderer.setTexture("gM", grey.data(), 4, 4, err, false, true));
    CHECK_TRUE(renderer.textureIsSrgb("gM"));
    renderer.setActiveTexture("gM");
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshTextured("w", Mat4::identity(), FillMode::Solid);
    renderer.endScenePass();
    {
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        if (!px.empty()) {
            int r = 0, g = 0, b = 0;
            centerRgb(px, r, g, b);
            printf("    nomips center pixel: %d %d %d\n", r, g, b);
            CHECK_TRUE(r > 55 && r < 105);
        }
    }

    // 5. Flag lifecycle: re-upload flips the flag, release clears it.
    CHECK_TRUE(renderer.setTexture("gS", grey.data(), 4, 4, err, true, false));
    CHECK_FALSE(renderer.textureIsSrgb("gS"));
    renderer.releaseTexture("gS");
    CHECK_FALSE(renderer.textureIsSrgb("gS"));

    renderer.releaseMesh("f");
    renderer.releaseMesh("w");
    renderer.releaseTexture("gL");
    renderer.releaseTexture("gM");
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

M2RIG_TEST(render, gpu_skinning_lbs) {
    // Pins the Wave-24 GPU skinning path: identity palette reproduces the
    // static draw bit-exactly, a translated palette moves the mesh, and the
    // textured/flat/overlay skinned variants emit correct pixels.
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigSkinTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    // Rigid synthetic mesh: fullscreen triangle, single bone, weight 1.
    Mesh synMesh;
    {
        const Vec3 pos[3] = {{-1.0f, -1.0f, 0.5f}, {3.0f, -1.0f, 0.5f}, {-1.0f, 3.0f, 0.5f}};
        for (int i = 0; i < 3; ++i) {
            Vertex v;
            v.position = pos[i];
            v.normal = {0.0f, 0.0f, 1.0f};
            v.uv0 = {0.0f, 0.0f};
            v.color = {1.0f, 1.0f, 1.0f, 1.0f};
            v.influences = {{0u, 1.0f}};
            synMesh.vertices.push_back(v);
        }
        synMesh.indices = {0, 1, 2};
    }
    auto skin = buildSkinVertices(synMesh, 1u);
    CHECK_TRUE(skin.succeeded());
    if (!skin.succeeded()) {
        renderer.shutdown();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    std::vector<GpuVertex> white(3);
    for (int i = 0; i < 3; ++i) {
        white[i].position = synMesh.vertices[static_cast<std::size_t>(i)].position;
        white[i].normal = {0.0f, 0.0f, 1.0f};
        white[i].color[0] = white[i].color[1] = white[i].color[2] = white[i].color[3] = 1.0f;
        white[i].uv[0] = white[i].uv[1] = 0.0f;
    }
    CHECK_TRUE(renderer.uploadMesh("ws", white, synMesh.indices, err));
    CHECK_TRUE(renderer.uploadSkinning("ws", skin.value(), err));
    CHECK_TRUE(renderer.hasSkinning("ws"));
    CHECK_FALSE(renderer.hasSkinning("nope"));
    CHECK_FALSE(renderer.uploadSkinning("empty", {}, err));  // honest reject

    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto readback = [&](std::vector<std::uint8_t>& px) {
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        CHECK_EQ(pw, 64);
        CHECK_EQ(ph, 64);
    };
    auto nonClear = [&](const std::vector<std::uint8_t>& px) {
        int n = 0;
        for (std::size_t i = 0; i + 3 < px.size(); i += 4)
            if (px[i] > 8 || px[i + 1] > 8 || px[i + 2] > 8) ++n;
        return n;
    };
    // 1. Identity palette == static draw, bit-exact (rigid weight 1.0:
    // v*I is exact in fp32, /1.0 is exact).
    std::vector<Mat4> identity(1u, Mat4::identity());
    renderer.setSkinningPalette(identity);
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshSkinned("ws", Mat4::identity(), FillMode::Solid);
    renderer.endScenePass();
    std::vector<std::uint8_t> pxSkin;
    readback(pxSkin);
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMesh("ws", Mat4::identity(), FillMode::Solid);
    renderer.endScenePass();
    std::vector<std::uint8_t> pxStatic;
    readback(pxStatic);
    if (pxSkin.size() == pxStatic.size() && !pxSkin.empty()) {
        int diff = 0;
        for (std::size_t i = 0; i < pxSkin.size(); ++i)
            if (pxSkin[i] != pxStatic[i]) ++diff;
        printf("    skinned-vs-static diff bytes: %d\n", diff);
        CHECK_EQ(diff, 0);
    } else {
        CHECK_TRUE(false);
    }
    // 2. Translated palette moves the mesh (+0.5 NDC X = 16 px on 64).
    std::vector<Mat4> moved(1u, Mat4::translation({0.5f, 0.0f, 0.0f}));
    renderer.setSkinningPalette(moved);
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshSkinned("ws", Mat4::identity(), FillMode::Solid);
    renderer.endScenePass();
    std::vector<std::uint8_t> pxMoved;
    readback(pxMoved);
    if (pxMoved.size() == pxSkin.size() && !pxMoved.empty()) {
        int diff = 0;
        for (std::size_t i = 0; i < pxMoved.size(); ++i)
            if (pxMoved[i] != pxSkin[i]) ++diff;
        printf("    moved-vs-bind diff bytes: %d\n", diff);
        CHECK_TRUE(diff > 100);
        CHECK_TRUE(nonClear(pxMoved) > 100);
    } else {
        CHECK_TRUE(false);
    }
    // 3. Textured-skinned variant: same mid-grey math as the static path.
    std::vector<std::uint8_t> grey(4 * 4 * 4, 128);
    CHECK_TRUE(renderer.setTexture("gs", grey.data(), 4, 4, err));
    renderer.setActiveTexture("gs");
    renderer.setSkinningPalette(identity);
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshTexturedSkinned("ws", Mat4::identity(), FillMode::Solid);
    renderer.endScenePass();
    {
        std::vector<std::uint8_t> px;
        readback(px);
        if (!px.empty()) {
            const std::size_t c = (static_cast<std::size_t>(32) * 64u + 32u) * 4u;
            const int r = static_cast<int>(px[c]);
            printf("    skinned-textured center: %d\n", r);
            CHECK_TRUE(r > 55 && r < 105);
        }
    }
    renderer.setActiveTexture({});
    // 4. Flat-skinned variant: red passthrough on a second rigid mesh.
    std::vector<GpuVertex> red = white;
    for (auto& v : red) {
        v.color[0] = 1.0f;
        v.color[1] = 0.0f;
        v.color[2] = 0.0f;
    }
    CHECK_TRUE(renderer.uploadMesh("fr", red, synMesh.indices, err));
    CHECK_TRUE(renderer.uploadSkinning("fr", skin.value(), err));
    renderer.setSkinningPalette(identity);
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshFlatSkinned("fr", Mat4::identity(), FillMode::Solid);
    renderer.endScenePass();
    {
        std::vector<std::uint8_t> px;
        readback(px);
        if (!px.empty()) {
            const std::size_t c = (static_cast<std::size_t>(32) * 64u + 32u) * 4u;
            const int r = static_cast<int>(px[c]);
            const int g = static_cast<int>(px[c + 1]);
            const int b = static_cast<int>(px[c + 2]);
            printf("    skinned-flat center: %d %d %d\n", r, g, b);
            CHECK_TRUE(r > 245 && g < 10 && b < 10);
        }
    }
    // 5. Overlay-skinned variant emits wireframe pixels. Proof geometry is
    // strictly interior: boundary-hugging edges rasterize
    // implementation-defined counts (the fullscreen triangle's hypotenuse
    // misses the viewport and one boundary edge drops to fill rules), so an
    // interior triangle gives a robust perimeter (~109 px on 64px).
    std::vector<GpuVertex> inner = white;
    inner[0].position = {-0.5f, -0.5f, 0.5f};
    inner[1].position = {0.5f, -0.5f, 0.5f};
    inner[2].position = {-0.5f, 0.5f, 0.5f};
    CHECK_TRUE(renderer.uploadMesh("wo", inner, synMesh.indices, err));
    CHECK_TRUE(renderer.uploadSkinning("wo", skin.value(), err));
    renderer.setSkinningPalette(identity);
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshWireOverlaySkinned("wo", Mat4::identity());
    renderer.endScenePass();
    {
        std::vector<std::uint8_t> px;
        readback(px);
        if (!px.empty()) {
            const int n = nonClear(px);
            printf("    skinned-overlay non-clear: %d\n", n);
            CHECK_TRUE(n > 80);
        }
    }
    // releaseMesh drops the paired skin stream (stale-pairing guard).
    renderer.releaseMesh("wo");
    renderer.releaseMesh("ws");
    CHECK_FALSE(renderer.hasSkinning("ws"));
    // 6. Real multi-influence mesh (sample armor) draws skinned at bind.
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (sample.succeeded()) {
        const Mesh& mesh = sample.value().mesh;
        auto sk = buildSkinVertices(mesh, sample.value().skeleton.bones.size());
        CHECK_TRUE(sk.succeeded());
        if (sk.succeeded()) {
            CHECK_TRUE(renderer.uploadMesh("sa", buildGpuVertices(mesh, MeshColoring::Solid),
                                            mesh.indices, err));
            CHECK_TRUE(renderer.uploadSkinning("sa", sk.value(), err));
            renderer.setSkinningPalette(
                buildSkinningPalette(sample.value().skeleton,
                                     currentBindPalette(sample.value().skeleton)));
            const Mat4 vp = Mat4::lookAt({0, 2, 5}, {0, 1, 0}, {0, 1, 0}) *
                            Mat4::perspectiveFov(0.9f, 1.0f, 0.1f, 100.0f);
            CHECK_TRUE(renderer.beginScenePass(-8, -8, 80, 80, clear));
            renderer.drawMeshSkinned("sa", vp, FillMode::Solid);
            renderer.endScenePass();
            std::vector<std::uint8_t> px;
            readback(px);
            if (!px.empty()) {
                const int n = nonClear(px);
                printf("    sample-armor skinned non-clear: %d\n", n);
                CHECK_TRUE(n > 100);
            }
            renderer.releaseMesh("sa");
        }
    }
    renderer.releaseMesh("fr");
    renderer.releaseTexture("gs");
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

namespace {

// Independent C++ transcription of the HLSL PBR+IBL chain for ONE pixel
// (cross-check, not shared code): punctual GGX mirrors PbrLighting,
// SH diffuse uses the tested core, prefilter/BRDF are numeric integrations
// of the same split-sum formulation. Shared with the GPU path: scene
// constants only (sky params, default light, sRGB curves).
float refRadicalInverse(std::uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

Vec2 refHammersley(int i, int n) {
    return {((static_cast<float>(i)) + 0.5f) / static_cast<float>(n),
            refRadicalInverse(static_cast<std::uint32_t>(i))};
}

Vec3 refImportanceGGX(Vec2 Xi, Vec3 N, float rough) {
    const float a = rough * rough;
    const float phi = 2.0f * kPi * Xi.x;
    float denom = 1.0f + (a * a - 1.0f) * Xi.y;
    if (denom < 1e-6f) denom = 1e-6f;
    const float cosT = std::sqrt(std::max((1.0f - Xi.y) / denom, 0.0f));
    const float sinT = std::sqrt(std::max(1.0f - cosT * cosT, 0.0f));
    const Vec3 H{std::cos(phi) * sinT, std::sin(phi) * sinT, cosT};
    const Vec3 up = std::fabs(N.z) < 0.999f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const Vec3 tx = normalized(cross(up, N));
    const Vec3 ty = cross(N, tx);
    return normalized(tx * H.x + ty * H.y + N * H.z);
}

float refSmithJoint(float NoV, float NoL, float a) {
    const float a2 = a * a;
    const float ggxV = NoV * std::sqrt(std::max((-NoV * a2 + NoV) * NoV + a2, 1e-6f));
    const float ggxL = NoL * std::sqrt(std::max((-NoL * a2 + NoL) * NoL + a2, 1e-6f));
    return 0.5f / std::max(ggxV + ggxL, 1e-4f);
}

Vec3 refPrefilter(const IblSkyParams& sky, Vec3 R, float rough, int samples) {
    Vec3 acc{0, 0, 0};
    double wsum = 0.0;
    for (int i = 0; i < samples; ++i) {
        const Vec3 H = refImportanceGGX(refHammersley(i, samples), R, rough);
        Vec3 L = H * (2.0f * dot(R, H)) - R;
        L = normalized(L);
        const float NoL = std::max(dot(R, L), 0.0f);
        if (NoL > 0.0f) {
            acc += evalSky(sky, L) * NoL;
            wsum += NoL;
        }
    }
    return wsum > 1e-4 ? acc / static_cast<float>(wsum) : Vec3{0, 0, 0};
}

Vec2 refBrdf(float NoV, float rough, int samples) {
    const Vec3 V{std::sqrt(std::max(1.0f - NoV * NoV, 0.0f)), 0.0f, NoV};
    const Vec3 N{0, 0, 1};
    double A = 0.0, B = 0.0;
    for (int i = 0; i < samples; ++i) {
        const Vec3 H = refImportanceGGX(refHammersley(i, samples), N, rough);
        Vec3 L = H * (2.0f * dot(V, H)) - V;
        L = normalized(L);
        const float NoL = std::max(L.z, 0.0f);
        const float NoH = std::max(dot(N, H), 0.0f);
        const float VoH = std::max(dot(V, H), 0.0f);
        if (NoL > 0.0f && NoH > 0.0f && NoV > 0.0f) {
            const float Gv = refSmithJoint(NoV, NoL, rough * rough);
            const float Gvis = Gv * VoH / std::max(NoH * NoV, 1e-4f);
            const float Fc = std::pow(1.0f - VoH, 5.0f);
            A += (1.0 - Fc) * Gvis;
            B += Fc * Gvis;
        }
    }
    return {static_cast<float>(A / samples), static_cast<float>(B / samples)};
}

float refSrgbEncode(float x) {
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

float refSrgbDecode(float c) {
    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

struct RefRgb {
    int r = -1, g = -1, b = -1;
};

// Expected display bytes for one PBR pixel under the test setup (identity
// view rotation, default light, fullscreen triangle). Mirrors PbrLighting.
RefRgb refPbrPixel(const Vec3& Nw, const Vec3& albedo, float metal, float rough, float ao,
                   const Vec3& emis, float emisI, const IblSkyParams& sky,
                   const SphericalHarmonics& sh) {
    const Vec3 L = normalized(Vec3{0.4f, 0.8f, 0.45f});
    const Vec3 V{0, 0, 1};
    const float m = metal < 0.0f ? 0.0f : (metal > 1.0f ? 1.0f : metal);
    float ro = rough < 0.05f ? 0.05f : (rough > 1.0f ? 1.0f : rough);
    const float NoL = std::max(dot(Nw, L), 0.0f);
    const float NoV = std::max(dot(Nw, V), 0.0f);
    Vec3 H = L + V;
    H = normalized(H);
    const float NoH = std::max(dot(Nw, H), 0.0f);
    const float VoH = std::max(dot(V, H), 0.0f);
    const Vec3 F0 = Vec3{0.04f, 0.04f, 0.04f} + (albedo - Vec3{0.04f, 0.04f, 0.04f}) * m;
    const float a = ro * ro;
    const float dd = NoH * NoH * (a * a - 1.0f) + 1.0f;
    const float D = (a * a) / (kPi * dd * dd);
    const float kk = (a + 1.0f) * (a + 1.0f) / 8.0f;
    const float G =
        (NoV / (NoV * (1.0f - kk) + kk)) * (NoL / (NoL * (1.0f - kk) + kk));
    const Vec3 F = F0 + (Vec3{1, 1, 1} - F0) * std::pow(1.0f - VoH, 5.0f);
    const Vec3 spec = F * (D * G / std::max(4.0f * NoV * NoL, 1e-4f));
    const Vec3 diff = albedo * ((1.0f - m) / kPi * NoL);
    const Vec3 irr = shIrradiance(sh, Nw);
    const Vec3 diffIBL = Vec3{irr.x * albedo.x, irr.y * albedo.y, irr.z * albedo.z} *
                         ((1.0f - m) / kPi * ao);
    const Vec3 R = (Nw * (2.0f * dot(Nw, V)) - V);
    const Vec3 pre = refPrefilter(sky, normalized(R), ro, 128);
    const Vec2 brdf = refBrdf(NoV, ro, 128);
    const Vec3 specIBL =
        Vec3{pre.x * (F0.x * brdf.x + brdf.y) * ao, pre.y * (F0.y * brdf.x + brdf.y) * ao,
             pre.z * (F0.z * brdf.x + brdf.y) * ao};
    const Vec3 lit = diffIBL + specIBL + diff + spec + emis * emisI;
    RefRgb out;
    out.r = static_cast<int>(refSrgbEncode(lit.x) * 255.0f + 0.5f);
    out.g = static_cast<int>(refSrgbEncode(lit.y) * 255.0f + 0.5f);
    out.b = static_cast<int>(refSrgbEncode(lit.z) * 255.0f + 0.5f);
    return out;
}

}  // namespace

M2RIG_TEST(render, pbr_punctual_values) {
    // Pins the Wave-25a punctual PBR backend with hand-computed pixels
    // (setup mirrors the sRGB test: default light, identity WVP, white
    // fullscreen triangle). Metallic white ~98, dielectric white ~118,
    // sharp-roughness white-hot, grey base ~85, AO ordering, emissive lift,
    // textured ~56, skinned variants identical to their static twins.
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigPbrTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    // IBL reference scene (shared constants with the GPU bake).
    const IblSkyParams sky = defaultSky();
    const SphericalHarmonics sh = projectSky(sky);
    Mesh mesh;
    {
        const Vec3 pos[3] = {{-1.0f, -1.0f, 0.5f}, {3.0f, -1.0f, 0.5f}, {-1.0f, 3.0f, 0.5f}};
        for (int i = 0; i < 3; ++i) {
            Vertex v;
            v.position = pos[i];
            v.normal = {0.0f, 0.0f, 1.0f};
            v.uv0 = {0.0f, 0.0f};
            v.color = {1.0f, 1.0f, 1.0f, 1.0f};
            v.influences = {{0u, 1.0f}};
            mesh.vertices.push_back(v);
        }
        mesh.indices = {0, 1, 2};
    }
    std::vector<GpuVertex> white(3);
    for (int i = 0; i < 3; ++i) {
        white[i].position = mesh.vertices[static_cast<std::size_t>(i)].position;
        white[i].normal = {0.0f, 0.0f, 1.0f};
        white[i].color[0] = white[i].color[1] = white[i].color[2] = white[i].color[3] = 1.0f;
        white[i].uv[0] = white[i].uv[1] = 0.0f;
    }
    CHECK_TRUE(renderer.uploadMesh("pw", white, mesh.indices, err));
    auto skin = buildSkinVertices(mesh, 1u);
    CHECK_TRUE(skin.succeeded());
    if (skin.succeeded()) CHECK_TRUE(renderer.uploadSkinning("pw", skin.value(), err));

    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto shot = [&](std::function<void()> draw, int& r, int& g, int& b) {
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        draw();
        renderer.endScenePass();
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        r = g = b = -1;
        if (!px.empty()) {
            const std::size_t c = (static_cast<std::size_t>(32) * 64u + 32u) * 4u;
            r = static_cast<int>(px[c]);
            g = static_cast<int>(px[c + 1]);
            b = static_cast<int>(px[c + 2]);
        }
    };
    int r = 0, g = 0, b = 0;
    // 1. Metallic white (metal 1, rough 0.5): F0 == albedo, no diffuse.
    {
        PbrMaterial m;
        m.metallic = 1.0f;
        m.roughness = 0.5f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("pw", Mat4::identity(), FillMode::Solid); }, r, g, b);
        printf("    pbr metallic center: %d %d %d\n", r, g, b);
        {
            // IBL era: absolute 25a bands retired; the CPU reference decides.
            const RefRgb e = refPbrPixel({0, 0, 1}, {1, 1, 1}, 1.0f, 0.5f, 1.0f, {0, 0, 0},
                                         0.0f, sky, sh);
            printf("    pbr metallic reference: %d %d %d\n", e.r, e.g, e.b);
            CHECK_TRUE(abs(r - e.r) <= 12 && abs(g - e.g) <= 12 && abs(b - e.b) <= 12);
        }
    }
    // 2. Dielectric white (metal 0, rough 1): diffuse-dominated.
    {
        PbrMaterial m;
        m.roughness = 1.0f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("pw", Mat4::identity(), FillMode::Solid); }, r, g, b);
        printf("    pbr dielectric center: %d %d %d\n", r, g, b);
        {
            const RefRgb e = refPbrPixel({0, 0, 1}, {1, 1, 1}, 0.0f, 1.0f, 1.0f, {0, 0, 0},
                                         0.0f, sky, sh);
            printf("    pbr dielectric reference: %d %d %d\n", e.r, e.g, e.b);
            CHECK_TRUE(abs(r - e.r) <= 12 && abs(g - e.g) <= 12 && abs(b - e.b) <= 12);
        }
    }
    // 3. Roughness response needs H≈N geometry: the flat (0,0,1) triangle
    // fixes noH=0.8512 off-lobe, where rough 0.05 correctly yields ~117
    // (hand-verified: D collapses off-peak, spec→0 — the shader was right,
    // the geometry couldn't discriminate). This half-vector-aligned triangle
    // (N = normalize(L+V) for the default light) puts the lobe peak on the
    // pixel: rough 0.05 saturates white-hot, rough 1.0 sits at ~151.
    int sharpR = 0, broadR = 0;
    {
        std::vector<GpuVertex> half = white;
        for (auto& v : half) {
            v.normal = {0.23464f, 0.46928f, 0.85123f};
        }
        CHECK_TRUE(renderer.uploadMesh("ph", half, mesh.indices, err));
        PbrMaterial m;
        m.roughness = 0.05f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("ph", Mat4::identity(), FillMode::Solid); }, r, g, b);
        printf("    pbr sharp center: %d %d %d\n", r, g, b);
        sharpR = r;
        {
            // IBL adds light everywhere: saturation no longer guaranteed on
            // this off-mirror geometry — the reference decides, and the
            // mirror-sun case below owns the white-hot pin.
            const RefRgb e =
                refPbrPixel({0.23464f, 0.46928f, 0.85123f}, {1, 1, 1}, 0.0f, 0.05f, 1.0f,
                            {0, 0, 0}, 0.0f, sky, sh);
            printf("    pbr sharp reference: %d %d %d\n", e.r, e.g, e.b);
            CHECK_TRUE(abs(r - e.r) <= 12 && abs(g - e.g) <= 12 && abs(b - e.b) <= 12);
        }
        m.roughness = 1.0f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("ph", Mat4::identity(), FillMode::Solid); }, r, g, b);
        printf("    pbr broad center: %d %d %d\n", r, g, b);
        broadR = r;
        {
            const RefRgb e = refPbrPixel({0.23464f, 0.46928f, 0.85123f}, {1, 1, 1}, 0.0f, 1.0f,
                                         1.0f, {0, 0, 0}, 0.0f, sky, sh);
            printf("    pbr broad reference: %d %d %d\n", e.r, e.g, e.b);
            CHECK_TRUE(abs(r - e.r) <= 12 && abs(g - e.g) <= 12 && abs(b - e.b) <= 12);
        }
        // Same geometry, same light: the sharper lobe can only add specular.
        CHECK_TRUE(sharpR >= broadR);
        renderer.releaseMesh("ph");
    }
    // 4. Grey base color scales the response.
    {
        PbrMaterial m;
        m.roughness = 1.0f;
        m.baseColor[0] = m.baseColor[1] = m.baseColor[2] = 0.5f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("pw", Mat4::identity(), FillMode::Solid); }, r, g, b);
        printf("    pbr greybase center: %d %d %d\n", r, g, b);
        {
            const RefRgb e = refPbrPixel({0, 0, 1}, {0.5f, 0.5f, 0.5f}, 0.0f, 1.0f, 1.0f,
                                         {0, 0, 0}, 0.0f, sky, sh);
            printf("    pbr greybase reference: %d %d %d\n", e.r, e.g, e.b);
            CHECK_TRUE(abs(r - e.r) <= 12 && abs(g - e.g) <= 12 && abs(b - e.b) <= 12);
        }
    }
    // 5. AO ordering: killing ambient darkens (relative assertion, robust).
    int aoOff = 0, aoOn = 0;
    {
        PbrMaterial m;
        m.roughness = 1.0f;
        m.ao = 0.0f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("pw", Mat4::identity(), FillMode::Solid); }, r, g, b);
        aoOff = r;
        m.ao = 1.0f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("pw", Mat4::identity(), FillMode::Solid); }, r, g, b);
        aoOn = r;
        printf("    pbr ao off/on: %d / %d\n", aoOff, aoOn);
        CHECK_TRUE(aoOff > 85 && aoOff < aoOn);
    }
    // 6. Emissive lifts the response.
    {
        PbrMaterial m;
        m.roughness = 1.0f;
        m.emissive[0] = m.emissive[1] = m.emissive[2] = 1.0f;
        m.emissive[3] = 0.5f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("pw", Mat4::identity(), FillMode::Solid); }, r, g, b);
        printf("    pbr emissive center: %d %d %d\n", r, g, b);
        {
            const RefRgb e = refPbrPixel({0, 0, 1}, {1, 1, 1}, 0.0f, 1.0f, 1.0f, {1, 1, 1},
                                         0.5f, sky, sh);
            printf("    pbr emissive reference: %d %d %d\n", e.r, e.g, e.b);
            CHECK_TRUE(abs(r - e.r) <= 12 && abs(g - e.g) <= 12 && abs(b - e.b) <= 12);
            CHECK_TRUE(r > 190);  // emissive lift survives IBL (robust absolute)
        }
    }
    // 7. Textured PBR: mid-grey albedo decodes like the Blinn path.
    {
        std::vector<std::uint8_t> grey(4 * 4 * 4, 128);
        CHECK_TRUE(renderer.setTexture("pgt", grey.data(), 4, 4, err));
        renderer.setActiveTexture("pgt");
        PbrMaterial m;
        m.roughness = 1.0f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshTexturedPbr("pw", Mat4::identity(), FillMode::Solid); }, r,
              g, b);
        printf("    pbr textured center: %d %d %d\n", r, g, b);
        {
            const float tex = refSrgbDecode(128.0f / 255.0f);
            const RefRgb e = refPbrPixel({0, 0, 1}, {tex, tex, tex}, 0.0f, 1.0f, 1.0f,
                                         {0, 0, 0}, 0.0f, sky, sh);
            printf("    pbr textured reference: %d %d %d\n", e.r, e.g, e.b);
            CHECK_TRUE(abs(r - e.r) <= 12 && abs(g - e.g) <= 12 && abs(b - e.b) <= 12);
        }
        renderer.setActiveTexture({});
        renderer.releaseTexture("pgt");
    }
    // 8. Skinned PBR twins match their static counterparts.
    {
        renderer.setSkinningPalette(std::vector<Mat4>(1u, Mat4::identity()));
        PbrMaterial m;
        m.roughness = 1.0f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshSkinnedPbr("pw", Mat4::identity(), FillMode::Solid); }, r,
              g, b);
        printf("    pbr skinned center: %d %d %d\n", r, g, b);
        {
            const RefRgb e = refPbrPixel({0, 0, 1}, {1, 1, 1}, 0.0f, 1.0f, 1.0f, {0, 0, 0},
                                         0.0f, sky, sh);
            printf("    pbr skinned reference: %d %d %d\n", e.r, e.g, e.b);
            CHECK_TRUE(abs(r - e.r) <= 12 && abs(g - e.g) <= 12 && abs(b - e.b) <= 12);
        }
        std::vector<std::uint8_t> grey(4 * 4 * 4, 128);
        CHECK_TRUE(renderer.setTexture("pgt", grey.data(), 4, 4, err));
        renderer.setActiveTexture("pgt");
        shot([&] { renderer.drawMeshTexturedSkinnedPbr("pw", Mat4::identity(), FillMode::Solid); },
              r, g, b);
        printf("    pbr textured-skinned center: %d %d %d\n", r, g, b);
        {
            const float tex = refSrgbDecode(128.0f / 255.0f);
            const RefRgb e = refPbrPixel({0, 0, 1}, {tex, tex, tex}, 0.0f, 1.0f, 1.0f,
                                         {0, 0, 0}, 0.0f, sky, sh);
            printf("    pbr textured-skinned reference: %d %d %d\n", e.r, e.g, e.b);
            CHECK_TRUE(abs(r - e.r) <= 12 && abs(g - e.g) <= 12 && abs(b - e.b) <= 12);
        }
        renderer.setActiveTexture({});
        renderer.releaseTexture("pgt");
    }
    renderer.releaseMesh("pw");
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

M2RIG_TEST(render, ibl_split_sum_matches_reference) {
    // End-to-end IBL proof against the independent CPU reference: dielectric
    // and metallic on the flat normal plus the mirror-sun case, whose
    // reflection must land on the sun disc through the whole chain (cube
    // orientation, mip selection, BRDF LUT). A mirrored CubeDir table would
    // point the mirror at empty sky and fail loudly here.
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigIblTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    const IblSkyParams sky = defaultSky();
    const SphericalHarmonics sh = projectSky(sky);
    auto triMesh = [&](const Vec3& nrm, const char* key) {
        std::vector<GpuVertex> tri(3);
        const Vec3 pos[3] = {{-1.0f, -1.0f, 0.5f}, {3.0f, -1.0f, 0.5f}, {-1.0f, 3.0f, 0.5f}};
        for (int i = 0; i < 3; ++i) {
            tri[i].position = pos[i];
            tri[i].normal = nrm;
            tri[i].color[0] = tri[i].color[1] = tri[i].color[2] = tri[i].color[3] = 1.0f;
            tri[i].uv[0] = tri[i].uv[1] = 0.0f;
        }
        CHECK_TRUE(renderer.uploadMesh(key, tri, {0, 1, 2}, err));
    };
    triMesh({0, 0, 1}, "iw");
    // Mirror normal: R = reflect(-V, N) == sunDir (same construction as the
    // 25a H-geometry; sunDir shared from defaultSky).
    const Vec3 mirrorN = normalized(sky.sunDir + Vec3{0, 0, 1});
    triMesh(mirrorN, "im");

    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto shot = [&](std::function<void()> draw, int& r, int& g, int& b) {
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        draw();
        renderer.endScenePass();
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        r = g = b = -1;
        if (!px.empty()) {
            const std::size_t c = (static_cast<std::size_t>(32) * 64u + 32u) * 4u;
            r = static_cast<int>(px[c]);
            g = static_cast<int>(px[c + 1]);
            b = static_cast<int>(px[c + 2]);
        }
    };
    auto checkRef = [&](const char* tag, const Vec3& Nw, const Vec3& albedo, float metal,
                        float rough, int r, int g, int b, int tol) {
        const RefRgb e =
            refPbrPixel(Nw, albedo, metal, rough, 1.0f, {0, 0, 0}, 0.0f, sky, sh);
        printf("    ibl %s: gpu %d %d %d vs ref %d %d %d\n", tag, r, g, b, e.r, e.g, e.b);
        CHECK_TRUE(abs(r - e.r) <= tol && abs(g - e.g) <= tol && abs(b - e.b) <= tol);
    };
    int r = 0, g = 0, b = 0;
    {
        PbrMaterial m;
        m.roughness = 1.0f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("iw", Mat4::identity(), FillMode::Solid); }, r, g, b);
        checkRef("dielectric", {0, 0, 1}, {1, 1, 1}, 0.0f, 1.0f, r, g, b, 12);
    }
    {
        PbrMaterial m;
        m.metallic = 1.0f;
        m.roughness = 0.5f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("iw", Mat4::identity(), FillMode::Solid); }, r, g, b);
        checkRef("metallic", {0, 0, 1}, {1, 1, 1}, 1.0f, 0.5f, r, g, b, 12);
    }
    {
        // Mirror sun: the reflection must be bright (sun disc through the
        // chain), and match the reference. ±15 tolerates mip/texel
        // discretization of the small disc.
        PbrMaterial m;
        m.metallic = 1.0f;
        m.roughness = 0.05f;
        renderer.setPbrMaterial(m);
        shot([&] { renderer.drawMeshPbr("im", Mat4::identity(), FillMode::Solid); }, r, g, b);
        checkRef("mirror-sun", mirrorN, {1, 1, 1}, 1.0f, 0.05f, r, g, b, 15);
        CHECK_TRUE(r > 150);  // orientation tripwire: mirrored cube = dim sky
    }
    renderer.releaseMesh("iw");
    renderer.releaseMesh("im");
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

M2RIG_TEST(render, offscreen_72B_viewport_pass) {
    // Pins the production OFFSCREEN path (ensureViewportTarget /
    // beginViewportPass / endViewportPass / readViewport): the backbuffer
    // tests above do not touch it. Same 72 B Solid sample-armor mesh and
    // the same model-proof WVP as headless_frame_through_all_paths, so the
    // pixel expectation transfers directly (non-clear > 100 on 64x64).
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigOffscreenTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) {
        renderer.shutdown();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    const Mesh& mesh = sample.value().mesh;
    CHECK_TRUE(
        renderer.uploadMesh("o", buildGpuVertices(mesh, MeshColoring::Solid), mesh.indices, err));
    const Mat4 vp = Mat4::lookAt({0, 2, 5}, {0, 1, 0}, {0, 1, 0}) *
                    Mat4::perspectiveFov(0.9f, 1.0f, 0.1f, 100.0f);
    const float clear[4] = {0.09f, 0.10f, 0.13f, 1.0f};
    auto countNonClear = [&](const std::vector<std::uint8_t>& pixels) {
        const int cr = static_cast<int>(clear[0] * 255.0f + 0.5f);
        const int cg = static_cast<int>(clear[1] * 255.0f + 0.5f);
        const int cb = static_cast<int>(clear[2] * 255.0f + 0.5f);
        int count = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
            const int dr = static_cast<int>(pixels[i + 0]) - cr;
            const int dg = static_cast<int>(pixels[i + 1]) - cg;
            const int db = static_cast<int>(pixels[i + 2]) - cb;
            if (dr * dr + dg * dg + db * db > 12 * 12 * 3) ++count;
        }
        return count;
    };
    CHECK_TRUE(renderer.ensureViewportTarget(64, 64, err));
    CHECK_TRUE(renderer.viewportSrv() != nullptr);
    CHECK_TRUE(renderer.beginViewportPass(clear));
    renderer.drawMesh("o", vp, FillMode::Solid);
    renderer.endViewportPass();
    {
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readViewport(px, pw, ph));
        CHECK_EQ(pw, 64);
        CHECK_EQ(ph, 64);
        CHECK_EQ(px.size(), static_cast<std::size_t>(64 * 64 * 4));
        if (!px.empty()) {
            const int nonClear = countNonClear(px);
            printf("    offscreen pixels: %d non-clear of %d\n", nonClear, pw * ph);
            CHECK_TRUE(nonClear > 100);
        }
    }
    // Resize path: same mesh, smaller target, second pass still emits pixels.
    // Quarter area, so the threshold scales down (64px proof is > 100).
    CHECK_TRUE(renderer.ensureViewportTarget(32, 32, err));
    CHECK_TRUE(renderer.viewportSrv() != nullptr);
    CHECK_TRUE(renderer.beginViewportPass(clear));
    renderer.drawMesh("o", vp, FillMode::Solid);
    renderer.endViewportPass();
    {
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readViewport(px, pw, ph));
        CHECK_EQ(pw, 32);
        CHECK_EQ(ph, 32);
        CHECK_EQ(px.size(), static_cast<std::size_t>(32 * 32 * 4));
        if (!px.empty()) {
            const int nonClear = countNonClear(px);
            printf("    offscreen pixels (32): %d non-clear of %d\n", nonClear, pw * ph);
            CHECK_TRUE(nonClear > 25);
        }
    }
    renderer.releaseMesh("o");
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

M2RIG_TEST(render, authored_mips_upload_idempotent_and_ranges) {
    // Pins Wave-27 Slice C item 1 (setTextureMips: authored chain, no
    // GenerateMips) + item 4 minimal ranges. Same mid-grey math as the sRGB
    // test: solid 128 texels must land in the 55..105 sRGB band, and
    // re-uploading the same key must replace idempotently.
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigAuthoredMipsTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    // Fullscreen white triangle (same proof geometry as the sRGB test).
    std::vector<GpuVertex> whiteTri(3);
    {
        const Vec3 pos[3] = {{-1.0f, -1.0f, 0.5f}, {3.0f, -1.0f, 0.5f}, {-1.0f, 3.0f, 0.5f}};
        for (int i = 0; i < 3; ++i) {
            whiteTri[static_cast<std::size_t>(i)].position = pos[i];
            whiteTri[static_cast<std::size_t>(i)].normal = {0.0f, 0.0f, 1.0f};
            whiteTri[static_cast<std::size_t>(i)].tangent = {1.0f, 0.0f, 0.0f};
            whiteTri[static_cast<std::size_t>(i)].bitangent = {0.0f, 1.0f, 0.0f};
            whiteTri[static_cast<std::size_t>(i)].color[0] = 1.0f;
            whiteTri[static_cast<std::size_t>(i)].color[1] = 1.0f;
            whiteTri[static_cast<std::size_t>(i)].color[2] = 1.0f;
            whiteTri[static_cast<std::size_t>(i)].color[3] = 1.0f;
            whiteTri[static_cast<std::size_t>(i)].uv[0] = 0.0f;
            whiteTri[static_cast<std::size_t>(i)].uv[1] = 0.0f;
        }
    }
    CHECK_TRUE(renderer.uploadMesh("w", whiteTri, {0, 1, 2}, err));
    // 4x4 -> 2x2 -> 1x1 solid-128 chain (same texel value as the sRGB pin so
    // the 55..105 band transfers directly). All bytes 128 matches the
    // existing grey fixture (alpha included).
    DdsImage img;
    img.width = 4;
    img.height = 4;
    img.format = "DXT1";
    {
        DdsMipLevel l0;
        l0.width = 4;
        l0.height = 4;
        l0.rgba.assign(4u * 4u * 4u, 128);
        DdsMipLevel l1;
        l1.width = 2;
        l1.height = 2;
        l1.rgba.assign(2u * 2u * 4u, 128);
        DdsMipLevel l2;
        l2.width = 1;
        l2.height = 1;
        l2.rgba.assign(4u, 128);
        img.mips.push_back(std::move(l0));
        img.mips.push_back(std::move(l1));
        img.mips.push_back(std::move(l2));
    }
    img.mipCount = static_cast<std::uint32_t>(img.mips.size());
    img.rgba = img.mips[0].rgba;
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto centerR = [&](const std::vector<std::uint8_t>& p) {
        const std::size_t c = (static_cast<std::size_t>(32) * 64u + 32u) * 4u;
        return static_cast<int>(p[c]);
    };
    auto shotTextured = [&](int& r) {
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        renderer.drawMeshTextured("w", Mat4::identity(), FillMode::Solid);
        renderer.endScenePass();
        std::vector<std::uint8_t> px;
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        r = -1;
        if (!px.empty()) r = centerR(px);
    };
    CHECK_TRUE(renderer.setTextureMips("am", img, err));
    CHECK_TRUE(renderer.hasTexture("am"));
    CHECK_TRUE(renderer.textureIsSrgb("am"));
    renderer.setActiveTexture("am");
    int r1 = -1;
    shotTextured(r1);
    printf("    authored-mips center: %d\n", r1);
    CHECK_TRUE(r1 > 55 && r1 < 105);
    // Idempotence: re-uploading the same key replaces and draws identically.
    CHECK_TRUE(renderer.setTextureMips("am", img, err));
    CHECK_TRUE(renderer.hasTexture("am"));
    int r2 = -1;
    shotTextured(r2);
    printf("    authored-mips re-upload center: %d\n", r2);
    CHECK_TRUE(r2 > 55 && r2 < 105);
    CHECK_TRUE(abs(r2 - r1) <= 2);
    // Linear-flag variant selects the raw path (flag lifecycle pin).
    CHECK_TRUE(renderer.setTextureMips("amL", img, err, false));
    CHECK_FALSE(renderer.textureIsSrgb("amL"));
    // Honest rejects: empty chain and corrupt level size.
    {
        DdsImage bad;
        bad.width = 4;
        bad.height = 4;
        CHECK_FALSE(renderer.setTextureMips("badEmpty", bad, err));
        DdsImage badSize = img;
        badSize.mips[1].rgba.push_back(0);
        CHECK_FALSE(renderer.setTextureMips("badSize", badSize, err));
        CHECK_FALSE(renderer.hasTexture("badSize"));
    }
    // Range draws (Slice C minimal set): full-range solid matches the whole
    // draw byte-exactly; full-range textured matches the whole textured
    // center; out-of-range skips and overrun clamps without crashing.
    {
        auto readback = [&](std::vector<std::uint8_t>& px) {
            int pw = 0, ph = 0;
            CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
            CHECK_EQ(pw, 64);
            CHECK_EQ(ph, 64);
        };
        renderer.setActiveTexture({});
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        renderer.drawMesh("w", Mat4::identity(), FillMode::Solid);
        renderer.endScenePass();
        std::vector<std::uint8_t> pxWhole;
        readback(pxWhole);
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        renderer.drawMeshRange("w", 0, 3, Mat4::identity(), FillMode::Solid);
        renderer.endScenePass();
        std::vector<std::uint8_t> pxRange;
        readback(pxRange);
        if (pxWhole.size() == pxRange.size() && !pxWhole.empty()) {
            int diff = 0;
            for (std::size_t i = 0; i < pxWhole.size(); ++i)
                if (pxWhole[i] != pxRange[i]) ++diff;
            printf("    range-vs-whole diff bytes: %d\n", diff);
            CHECK_EQ(diff, 0);
        } else {
            CHECK_TRUE(false);
        }
        renderer.setActiveTexture("am");
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        renderer.drawMeshTexturedRange("w", 0, 3, Mat4::identity(), FillMode::Solid);
        renderer.endScenePass();
        std::vector<std::uint8_t> pxTexRange;
        readback(pxTexRange);
        if (!pxTexRange.empty()) {
            const int rr = centerR(pxTexRange);
            printf("    textured-range center: %d\n", rr);
            CHECK_TRUE(rr > 55 && rr < 105);
            CHECK_TRUE(abs(rr - r2) <= 2);
        }
        // Degenerate ranges must skip safely (no crash, no pixels asserted).
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        renderer.drawMeshRange("w", 999, 3, Mat4::identity(), FillMode::Solid);
        renderer.drawMeshRange("w", 0, 0, Mat4::identity(), FillMode::Solid);
        renderer.drawMeshRange("w", 1, 100, Mat4::identity(), FillMode::Solid);
        renderer.drawMeshTexturedRange("w", 999, 3, Mat4::identity(), FillMode::Solid);
        renderer.endScenePass();
    }
    renderer.setActiveTexture({});
    renderer.releaseTexture("am");
    renderer.releaseTexture("amL");
    CHECK_FALSE(renderer.hasTexture("am"));
    renderer.releaseMesh("w");
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

M2RIG_TEST(render, range_parity) {
    // Pins Slice C2: each new range vs its whole-draw twin on split
    // geometry (interior quad, 4 verts / 6 indices). Full-range (0,6)
    // must be byte-identical to the whole draw; half-range (0,3) must emit
    // pixels; degenerate (count 0 / start OOB) and overrun (0,100 clamped
    // to 6) must not crash (overrun equals whole).
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigRangeParityTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    // Interior quad (robust for solid + wire overlay: no boundary-hugging
    // edges, ~1024 px solid / ~100 px wire on 64x64).
    std::vector<GpuVertex> quad(4);
    {
        const Vec3 pos[4] = {{-0.5f, -0.5f, 0.5f},
                             {0.5f, -0.5f, 0.5f},
                             {0.5f, 0.5f, 0.5f},
                             {-0.5f, 0.5f, 0.5f}};
        for (int i = 0; i < 4; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            quad[k].position = pos[i];
            quad[k].normal = {0.0f, 0.0f, 1.0f};
            quad[k].tangent = {1.0f, 0.0f, 0.0f};
            quad[k].bitangent = {0.0f, 1.0f, 0.0f};
            quad[k].color[0] = 1.0f;
            quad[k].color[1] = 1.0f;
            quad[k].color[2] = 1.0f;
            quad[k].color[3] = 1.0f;
            quad[k].uv[0] = 0.0f;
            quad[k].uv[1] = 0.0f;
        }
    }
    const std::vector<std::uint32_t> quadIdx = {0, 1, 2, 0, 2, 3};
    CHECK_TRUE(renderer.uploadMesh("split", quad, quadIdx, err));
    // Rigid skin (bone 0, weight 1) for the skinned twins.
    std::vector<SkinVertex> skin(4);
    for (std::size_t k = 0; k < skin.size(); ++k) {
        skin[k].bones[0] = 0u;
        skin[k].bones[1] = 0u;
        skin[k].bones[2] = 0u;
        skin[k].bones[3] = 0u;
        skin[k].weights[0] = 1.0f;
        skin[k].weights[1] = 0.0f;
        skin[k].weights[2] = 0.0f;
        skin[k].weights[3] = 0.0f;
    }
    CHECK_TRUE(renderer.uploadSkinning("split", skin, err));
    renderer.setSkinningPalette(std::vector<Mat4>(1u, Mat4::identity()));
    std::vector<std::uint8_t> grey(4 * 4 * 4, 128);
    CHECK_TRUE(renderer.setTexture("grey", grey.data(), 4, 4, err));
    {
        PbrMaterial m;
        m.roughness = 1.0f;
        renderer.setPbrMaterial(m);
    }
    renderer.clearPbrNormalMap();

    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto readback = [&](std::vector<std::uint8_t>& px) {
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        CHECK_EQ(pw, 64);
        CHECK_EQ(ph, 64);
    };
    auto diffBytes = [&](const std::vector<std::uint8_t>& a,
                         const std::vector<std::uint8_t>& b) {
        if (a.size() != b.size() || a.empty()) return -1;
        int d = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i]) ++d;
        return d;
    };
    auto nonClear = [&](const std::vector<std::uint8_t>& px) {
        int n = 0;
        for (std::size_t i = 0; i + 3 < px.size(); i += 4)
            if (px[i] > 8 || px[i + 1] > 8 || px[i + 2] > 8) ++n;
        return n;
    };
    // One parity cell: whole vs full-range diff==0, half-range emits,
    // overrun (0,100) equals whole. Degenerate skips are exercised per
    // variant below (no crash, no pixel assert like Slice C).
    auto checkCell = [&](const char* tag, const std::function<void()>& whole,
                         const std::function<void()>& full,
                         const std::function<void()>& half,
                         const std::function<void()>& overrun) {
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        whole();
        renderer.endScenePass();
        std::vector<std::uint8_t> pxWhole;
        readback(pxWhole);
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        full();
        renderer.endScenePass();
        std::vector<std::uint8_t> pxFull;
        readback(pxFull);
        const int d = diffBytes(pxWhole, pxFull);
        printf("    range parity %s full diff: %d\n", tag, d);
        CHECK_EQ(d, 0);
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        half();
        renderer.endScenePass();
        std::vector<std::uint8_t> pxHalf;
        readback(pxHalf);
        const int n = nonClear(pxHalf);
        printf("    range parity %s half non-clear: %d\n", tag, n);
        CHECK_TRUE(n > 50);
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        overrun();
        renderer.endScenePass();
        std::vector<std::uint8_t> pxOver;
        readback(pxOver);
        const int dOver = diffBytes(pxWhole, pxOver);
        printf("    range parity %s overrun diff: %d\n", tag, dOver);
        CHECK_EQ(dOver, 0);
    };
    const Mat4 id = Mat4::identity();
    // 1. Flat.
    checkCell(
        "flat", [&] { renderer.drawMeshFlat("split", id, FillMode::Solid); },
        [&] { renderer.drawMeshFlatRange("split", 0, 6, id, FillMode::Solid); },
        [&] { renderer.drawMeshFlatRange("split", 0, 3, id, FillMode::Solid); },
        [&] { renderer.drawMeshFlatRange("split", 0, 100, id, FillMode::Solid); });
    // 2. Wire overlay.
    checkCell(
        "overlay", [&] { renderer.drawMeshWireOverlay("split", id); },
        [&] { renderer.drawMeshWireOverlayRange("split", 0, 6, id); },
        [&] { renderer.drawMeshWireOverlayRange("split", 0, 3, id); },
        [&] { renderer.drawMeshWireOverlayRange("split", 0, 100, id); });
    // 3. Skinned.
    checkCell(
        "skinned", [&] { renderer.drawMeshSkinned("split", id, FillMode::Solid); },
        [&] { renderer.drawMeshSkinnedRange("split", 0, 6, id, FillMode::Solid); },
        [&] { renderer.drawMeshSkinnedRange("split", 0, 3, id, FillMode::Solid); },
        [&] { renderer.drawMeshSkinnedRange("split", 0, 100, id, FillMode::Solid); });
    // 4. Textured-skinned (albedo bound).
    renderer.setActiveTexture("grey");
    checkCell(
        "texskinned", [&] { renderer.drawMeshTexturedSkinned("split", id, FillMode::Solid); },
        [&] { renderer.drawMeshTexturedSkinnedRange("split", 0, 6, id, FillMode::Solid); },
        [&] { renderer.drawMeshTexturedSkinnedRange("split", 0, 3, id, FillMode::Solid); },
        [&] { renderer.drawMeshTexturedSkinnedRange("split", 0, 100, id, FillMode::Solid); });
    renderer.setActiveTexture({});
    // 5. Flat-skinned.
    checkCell(
        "flatskinned", [&] { renderer.drawMeshFlatSkinned("split", id, FillMode::Solid); },
        [&] { renderer.drawMeshFlatSkinnedRange("split", 0, 6, id, FillMode::Solid); },
        [&] { renderer.drawMeshFlatSkinnedRange("split", 0, 3, id, FillMode::Solid); },
        [&] { renderer.drawMeshFlatSkinnedRange("split", 0, 100, id, FillMode::Solid); });
    // 6. Overlay-skinned.
    checkCell(
        "overlayskinned", [&] { renderer.drawMeshWireOverlaySkinned("split", id); },
        [&] { renderer.drawMeshWireOverlaySkinnedRange("split", 0, 6, id); },
        [&] { renderer.drawMeshWireOverlaySkinnedRange("split", 0, 3, id); },
        [&] { renderer.drawMeshWireOverlaySkinnedRange("split", 0, 100, id); });
    // 7. PBR.
    checkCell(
        "pbr", [&] { renderer.drawMeshPbr("split", id, FillMode::Solid); },
        [&] { renderer.drawMeshPbrRange("split", 0, 6, id, FillMode::Solid); },
        [&] { renderer.drawMeshPbrRange("split", 0, 3, id, FillMode::Solid); },
        [&] { renderer.drawMeshPbrRange("split", 0, 100, id, FillMode::Solid); });
    // 8. Textured-PBR (albedo bound, normal unbound => PsTexPbr).
    renderer.setActiveTexture("grey");
    checkCell(
        "texpbr", [&] { renderer.drawMeshTexturedPbr("split", id, FillMode::Solid); },
        [&] { renderer.drawMeshTexturedPbrRange("split", 0, 6, id, FillMode::Solid); },
        [&] { renderer.drawMeshTexturedPbrRange("split", 0, 3, id, FillMode::Solid); },
        [&] { renderer.drawMeshTexturedPbrRange("split", 0, 100, id, FillMode::Solid); });
    // 9. Skinned-PBR.
    checkCell(
        "skinnedpbr", [&] { renderer.drawMeshSkinnedPbr("split", id, FillMode::Solid); },
        [&] { renderer.drawMeshSkinnedPbrRange("split", 0, 6, id, FillMode::Solid); },
        [&] { renderer.drawMeshSkinnedPbrRange("split", 0, 3, id, FillMode::Solid); },
        [&] { renderer.drawMeshSkinnedPbrRange("split", 0, 100, id, FillMode::Solid); });
    // 10. Textured-skinned-PBR.
    checkCell(
        "texskinnedpbr",
        [&] { renderer.drawMeshTexturedSkinnedPbr("split", id, FillMode::Solid); },
        [&] { renderer.drawMeshTexturedSkinnedPbrRange("split", 0, 6, id, FillMode::Solid); },
        [&] { renderer.drawMeshTexturedSkinnedPbrRange("split", 0, 3, id, FillMode::Solid); },
        [&] {
            renderer.drawMeshTexturedSkinnedPbrRange("split", 0, 100, id, FillMode::Solid);
        });
    renderer.setActiveTexture({});
    // Degenerate + overrun skips for every variant (no crash; mirrors the
    // Slice C degenerate block which asserts nothing pixel-wise).
    CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
    renderer.drawMeshFlatRange("split", 999, 3, id, FillMode::Solid);
    renderer.drawMeshFlatRange("split", 0, 0, id, FillMode::Solid);
    renderer.drawMeshWireOverlayRange("split", 999, 3, id);
    renderer.drawMeshWireOverlayRange("split", 0, 0, id);
    renderer.drawMeshSkinnedRange("split", 999, 3, id, FillMode::Solid);
    renderer.drawMeshSkinnedRange("split", 0, 0, id, FillMode::Solid);
    renderer.drawMeshTexturedSkinnedRange("split", 999, 3, id, FillMode::Solid);
    renderer.drawMeshFlatSkinnedRange("split", 999, 3, id, FillMode::Solid);
    renderer.drawMeshWireOverlaySkinnedRange("split", 999, 3, id);
    renderer.drawMeshPbrRange("split", 999, 3, id, FillMode::Solid);
    renderer.drawMeshTexturedPbrRange("split", 999, 3, id, FillMode::Solid);
    renderer.drawMeshSkinnedPbrRange("split", 999, 3, id, FillMode::Solid);
    renderer.drawMeshTexturedSkinnedPbrRange("split", 999, 3, id, FillMode::Solid);
    renderer.drawMeshFlatRange("missing", 0, 6, id, FillMode::Solid);
    renderer.drawMeshSkinnedRange("missing", 0, 6, id, FillMode::Solid);
    renderer.endScenePass();

    renderer.releaseTexture("grey");
    renderer.releaseMesh("split");
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

M2RIG_TEST(render, normal_map_bound_vs_unbound) {
    // Pins the C2 normal path: unbound textured-PBR is byte-identical to
    // PsTexPbr across two draws; binding a synthetic tilted-normal streak
    // perturbs pixels; clearing restores the unbound bytes exactly.
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigNormalMapTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    // Fullscreen white triangle with a clean tangent frame (identity view
    // => object == view, so the passthrough tanView/bitanView equal view).
    std::vector<GpuVertex> tri(3);
    {
        const Vec3 pos[3] = {{-1.0f, -1.0f, 0.5f}, {3.0f, -1.0f, 0.5f}, {-1.0f, 3.0f, 0.5f}};
        for (int i = 0; i < 3; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            tri[k].position = pos[i];
            tri[k].normal = {0.0f, 0.0f, 1.0f};
            tri[k].tangent = {1.0f, 0.0f, 0.0f};
            tri[k].bitangent = {0.0f, 1.0f, 0.0f};
            tri[k].color[0] = 1.0f;
            tri[k].color[1] = 1.0f;
            tri[k].color[2] = 1.0f;
            tri[k].color[3] = 1.0f;
            tri[k].uv[0] = 0.0f;
            tri[k].uv[1] = 0.0f;
        }
    }
    CHECK_TRUE(renderer.uploadMesh("nt", tri, {0, 1, 2}, err));
    std::vector<std::uint8_t> grey(4 * 4 * 4, 128);
    CHECK_TRUE(renderer.setTexture("albedo", grey.data(), 4, 4, err));
    // Synthetic normal streak: solid tilted normal (192,128,255) =>
    // tangent-space (0.506, 0.004, 1.0), linear data (srgb=false) so every
    // mip stays the same tilt and any uv perturbs the geometric normal.
    std::vector<std::uint8_t> streak(4 * 4 * 4, 0);
    for (std::size_t i = 0; i < 16; ++i) {
        streak[i * 4 + 0] = 192;
        streak[i * 4 + 1] = 128;
        streak[i * 4 + 2] = 255;
        streak[i * 4 + 3] = 255;
    }
    CHECK_TRUE(renderer.setTexture("nstreak", streak.data(), 4, 4, err, true, false));
    {
        PbrMaterial m;
        m.roughness = 1.0f;
        renderer.setPbrMaterial(m);
    }
    renderer.setActiveTexture("albedo");
    renderer.clearPbrNormalMap();

    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto shot = [&](std::function<void()> draw, std::vector<std::uint8_t>& px) {
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        draw();
        renderer.endScenePass();
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        CHECK_EQ(pw, 64);
        CHECK_EQ(ph, 64);
    };
    const Mat4 id = Mat4::identity();
    std::vector<std::uint8_t> pxA, pxB;
    shot([&] { renderer.drawMeshTexturedPbr("nt", id, FillMode::Solid); }, pxA);
    shot([&] { renderer.drawMeshTexturedPbr("nt", id, FillMode::Solid); }, pxB);
    if (pxA.size() == pxB.size() && !pxA.empty()) {
        int diff = 0;
        for (std::size_t i = 0; i < pxA.size(); ++i)
            if (pxA[i] != pxB[i]) ++diff;
        printf("    normal unbound-vs-unbound diff bytes: %d\n", diff);
        CHECK_EQ(diff, 0);
    } else {
        CHECK_TRUE(false);
    }
    // Bound path must perturb the streak pixels.
    renderer.bindPbrNormalMap("nstreak");
    std::vector<std::uint8_t> pxN;
    shot([&] { renderer.drawMeshTexturedPbr("nt", id, FillMode::Solid); }, pxN);
    if (pxA.size() == pxN.size() && !pxA.empty()) {
        int diff = 0;
        for (std::size_t i = 0; i < pxA.size(); ++i)
            if (pxA[i] != pxN[i]) ++diff;
        printf("    normal bound-vs-unbound diff bytes: %d\n", diff);
        CHECK_TRUE(diff > 100);
        const std::size_t c = (static_cast<std::size_t>(32) * 64u + 32u) * 4u;
        const int dr = abs(static_cast<int>(pxN[c]) - static_cast<int>(pxA[c]));
        const int dg = abs(static_cast<int>(pxN[c + 1]) - static_cast<int>(pxA[c + 1]));
        const int db = abs(static_cast<int>(pxN[c + 2]) - static_cast<int>(pxA[c + 2]));
        printf("    normal center delta: %d %d %d\n", dr, dg, db);
        CHECK_TRUE(dr + dg + db > 5);
    } else {
        CHECK_TRUE(false);
    }
    // Clearing restores the unbound bytes exactly.
    renderer.clearPbrNormalMap();
    std::vector<std::uint8_t> pxC;
    shot([&] { renderer.drawMeshTexturedPbr("nt", id, FillMode::Solid); }, pxC);
    if (pxA.size() == pxC.size() && !pxA.empty()) {
        int diff = 0;
        for (std::size_t i = 0; i < pxA.size(); ++i)
            if (pxA[i] != pxC[i]) ++diff;
        printf("    normal cleared-vs-unbound diff bytes: %d\n", diff);
        CHECK_EQ(diff, 0);
    } else {
        CHECK_TRUE(false);
    }
    // Degenerate tangent frame falls back to the geometric normal: zero
    // tangents + bound streak must equal the unbound draw (same N0 path).
    {
        std::vector<GpuVertex> degen = tri;
        for (auto& v : degen) {
            v.tangent = {0.0f, 0.0f, 0.0f};
            v.bitangent = {0.0f, 0.0f, 0.0f};
        }
        CHECK_TRUE(renderer.uploadMesh("ndeg", degen, {0, 1, 2}, err));
        renderer.bindPbrNormalMap("nstreak");
        std::vector<std::uint8_t> pxDegBound, pxDegPlain;
        shot([&] { renderer.drawMeshTexturedPbr("ndeg", id, FillMode::Solid); }, pxDegBound);
        renderer.clearPbrNormalMap();
        shot([&] { renderer.drawMeshTexturedPbr("ndeg", id, FillMode::Solid); }, pxDegPlain);
        if (pxDegBound.size() == pxDegPlain.size() && !pxDegBound.empty()) {
            int diff = 0;
            for (std::size_t i = 0; i < pxDegBound.size(); ++i)
                if (pxDegBound[i] != pxDegPlain[i]) ++diff;
            printf("    normal degenerate fallback diff bytes: %d\n", diff);
            CHECK_EQ(diff, 0);
        } else {
            CHECK_TRUE(false);
        }
        renderer.releaseMesh("ndeg");
    }

    renderer.setActiveTexture({});
    renderer.clearPbrNormalMap();
    renderer.releaseTexture("albedo");
    renderer.releaseTexture("nstreak");
    renderer.releaseMesh("nt");
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

M2RIG_TEST(render, texture_cache_dedup_and_accounting) {
    // Pins the SRV content-hash cache: same bytes under two keys share one
    // GPU view (entries==1, bytes==once, identical pixels); different bytes
    // allocate distinctly (entries==2); release drops one ref (survives) then
    // the last ref (entries==0). Accounting (entries/bytes/hits/misses) is
    // pinned; eviction ORDER is NOT covered here — the cap is 256 MB and the
    // test deliberately does NOT allocate 256 MB (no fake coverage; the
    // zero-ref oldest-first sweep is defensive today since shared views are
    // destroyed at zero refs, so no zero-ref entries exist to evict).
    int failures = 0;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"M2RigTexCacheTest";
    CHECK_TRUE(RegisterClassExW(&wc) != 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    CHECK_TRUE(hwnd != nullptr);
    if (!hwnd) return failures + 1;

    Renderer renderer;
    std::string err;
    CHECK_TRUE(renderer.init(hwnd, 64, 64, err));
    if (!renderer.isInitialized()) {
        printf("    renderer init failed: %s\n", err.c_str());
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return failures + 1;
    }
    // Fullscreen white triangle (same proof geometry as the sRGB pin).
    std::vector<GpuVertex> whiteTri(3);
    {
        const Vec3 pos[3] = {{-1.0f, -1.0f, 0.5f}, {3.0f, -1.0f, 0.5f}, {-1.0f, 3.0f, 0.5f}};
        for (int i = 0; i < 3; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            whiteTri[k].position = pos[i];
            whiteTri[k].normal = {0.0f, 0.0f, 1.0f};
            whiteTri[k].color[0] = 1.0f;
            whiteTri[k].color[1] = 1.0f;
            whiteTri[k].color[2] = 1.0f;
            whiteTri[k].color[3] = 1.0f;
            whiteTri[k].uv[0] = 0.0f;
            whiteTri[k].uv[1] = 0.0f;
        }
    }
    CHECK_TRUE(renderer.uploadMesh("w", whiteTri, {0, 1, 2}, err));
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    // 4x4 generateMips chain: 16 + 4 + 1 texels, 4 B each = 84 B decoded.
    const std::size_t kChain84 =
        static_cast<std::size_t>(4u * 4u * 4u + 2u * 2u * 4u + 1u * 1u * 4u);
    auto centerR = [](const std::vector<std::uint8_t>& p) {
        const std::size_t c = (static_cast<std::size_t>(32) * 64u + 32u) * 4u;
        return static_cast<int>(p[c]);
    };
    auto shotActive = [&](const std::string& texKey, std::vector<std::uint8_t>& px) {
        renderer.setActiveTexture(texKey);
        CHECK_TRUE(renderer.beginScenePass(0, 0, 64, 64, clear));
        renderer.drawMeshTextured("w", Mat4::identity(), FillMode::Solid);
        renderer.endScenePass();
        int pw = 0, ph = 0;
        CHECK_TRUE(renderer.readBackbuffer(px, pw, ph));
        CHECK_EQ(pw, 64);
        CHECK_EQ(ph, 64);
    };
    {
        const Renderer::TextureCacheStats s0 = renderer.textureCacheStats();
        CHECK_EQ(s0.entries, static_cast<std::size_t>(0));
        CHECK_EQ(s0.bytes, static_cast<std::size_t>(0));
    }
    // hash_dedup: same 128 bytes under two keys share one entry.
    std::vector<std::uint8_t> grey(4 * 4 * 4, 128);
    CHECK_TRUE(renderer.setTexture("a", grey.data(), 4, 4, err));
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(1));
        CHECK_EQ(s.bytes, kChain84);
        CHECK_EQ(s.misses, static_cast<std::uint64_t>(1));
        CHECK_EQ(s.hits, static_cast<std::uint64_t>(0));
    }
    CHECK_TRUE(renderer.setTexture("b", grey.data(), 4, 4, err));
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(1));
        CHECK_EQ(s.bytes, kChain84);
        CHECK_EQ(s.misses, static_cast<std::uint64_t>(1));
        CHECK_EQ(s.hits, static_cast<std::uint64_t>(1));
    }
    CHECK_TRUE(renderer.hasTexture("a"));
    CHECK_TRUE(renderer.hasTexture("b"));
    // Both keys draw identical pixels (same bytes => same view + PS variant).
    std::vector<std::uint8_t> pxA, pxB;
    shotActive("a", pxA);
    shotActive("b", pxB);
    if (!pxA.empty() && pxA.size() == pxB.size()) {
        int diff = 0;
        for (std::size_t i = 0; i < pxA.size(); ++i)
            if (pxA[i] != pxB[i]) ++diff;
        printf("    cache dedup diff bytes: %d\n", diff);
        CHECK_EQ(diff, 0);
        const int r = centerR(pxA);
        printf("    cache dedup center: %d\n", r);
        CHECK_TRUE(r > 55 && r < 105);
    } else {
        CHECK_TRUE(false);
    }
    // distinct_bytes: different bytes allocate a second entry.
    std::vector<std::uint8_t> grey2(4 * 4 * 4, 200);
    CHECK_TRUE(renderer.setTexture("c", grey2.data(), 4, 4, err));
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(2));
        CHECK_EQ(s.bytes, kChain84 * 2u);
        CHECK_EQ(s.misses, static_cast<std::uint64_t>(2));
    }
    // release_drops_ref: releasing one alias survives, releasing both frees.
    renderer.setActiveTexture({});
    renderer.releaseTexture("a");
    CHECK_FALSE(renderer.hasTexture("a"));
    CHECK_TRUE(renderer.hasTexture("b"));
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(2));
        CHECK_EQ(s.bytes, kChain84 * 2u);
    }
    renderer.releaseTexture("b");
    CHECK_FALSE(renderer.hasTexture("b"));
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(1));
        CHECK_EQ(s.bytes, kChain84);
    }
    renderer.releaseTexture("c");
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(0));
        CHECK_EQ(s.bytes, static_cast<std::size_t>(0));
    }
    // srgb is part of the key: same bytes, different decode => distinct entry.
    CHECK_TRUE(renderer.setTexture("s1", grey.data(), 4, 4, err, true, true));
    CHECK_TRUE(renderer.setTexture("s2", grey.data(), 4, 4, err, true, false));
    CHECK_TRUE(renderer.textureIsSrgb("s1"));
    CHECK_FALSE(renderer.textureIsSrgb("s2"));
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(2));
        CHECK_EQ(s.bytes, kChain84 * 2u);
    }
    renderer.releaseTexture("s1");
    renderer.releaseTexture("s2");
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(0));
    }
    // Authored-mip dedup (setTextureMips path): same chain twice shares once.
    DdsImage img;
    img.width = 4;
    img.height = 4;
    img.format = "DXT1";
    {
        DdsMipLevel l0;
        l0.width = 4;
        l0.height = 4;
        l0.rgba.assign(4u * 4u * 4u, 128);
        DdsMipLevel l1;
        l1.width = 2;
        l1.height = 2;
        l1.rgba.assign(2u * 2u * 4u, 128);
        DdsMipLevel l2;
        l2.width = 1;
        l2.height = 1;
        l2.rgba.assign(4u, 128);
        img.mips.push_back(std::move(l0));
        img.mips.push_back(std::move(l1));
        img.mips.push_back(std::move(l2));
    }
    img.mipCount = static_cast<std::uint32_t>(img.mips.size());
    img.rgba = img.mips[0].rgba;
    CHECK_TRUE(renderer.setTextureMips("am1", img, err));
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(1));
        CHECK_EQ(s.bytes, kChain84);
    }
    CHECK_TRUE(renderer.setTextureMips("am2", img, err));
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(1));
        CHECK_EQ(s.bytes, kChain84);
    }
    std::vector<std::uint8_t> pxM1, pxM2;
    shotActive("am1", pxM1);
    shotActive("am2", pxM2);
    if (!pxM1.empty() && pxM1.size() == pxM2.size()) {
        int diff = 0;
        for (std::size_t i = 0; i < pxM1.size(); ++i)
            if (pxM1[i] != pxM2[i]) ++diff;
        printf("    cache authored dedup diff bytes: %d\n", diff);
        CHECK_EQ(diff, 0);
        CHECK_TRUE(centerR(pxM1) > 55 && centerR(pxM1) < 105);
    } else {
        CHECK_TRUE(false);
    }
    renderer.setActiveTexture({});
    renderer.releaseTexture("am1");
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(1));
    }
    renderer.releaseTexture("am2");
    {
        const Renderer::TextureCacheStats s = renderer.textureCacheStats();
        CHECK_EQ(s.entries, static_cast<std::size_t>(0));
        CHECK_EQ(s.bytes, static_cast<std::size_t>(0));
    }
    renderer.releaseMesh("w");
    renderer.shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return failures;
}

#else

M2RIG_TEST(render, headless_frame_through_all_paths) {
    // Non-Windows: renderer is Win32/D3D11 only.
    return 0;
}

#endif
