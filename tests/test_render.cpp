// Headless D3D11 render smoke test (Windows only): hidden window, WARP
// fallback allowed, one frame through every draw path (solid, wire,
// textured + fallback, overlay, lines, X-ray). Proves the renderer
// initializes, shaders compile, and the 52-byte vertex layout uploads.
#include <cstdio>

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
        renderer.beginScenePass(0, 0, 64, 64, clear);
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
        renderer.present(false);
        renderer.releaseMesh("t");
    }
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
