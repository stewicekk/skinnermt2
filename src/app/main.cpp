// Metin2 Rigging Studio — Win32 + DirectX 11 + Dear ImGui entry point.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "m2rig/app.hpp"
#include "m2rig/file_dialog.hpp"
#include "m2rig/logging.hpp"
#include "m2rig/panels.hpp"
#include "m2rig/renderer.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

m2rig::Renderer g_renderer;
bool g_resizePending = false;
int g_resizeW = 0, g_resizeH = 0;

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return 0;
    switch (msg) {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                g_resizeW = static_cast<int>(LOWORD(lparam));
                g_resizeH = static_cast<int>(HIWORD(lparam));
                g_resizePending = true;
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

std::filesystem::path executableDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

void applyDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.94f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.56f, 0.60f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.12f, 0.16f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.18f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.28f, 0.34f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.17f, 0.22f, 1.0f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.32f, 0.38f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.40f, 0.48f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.44f, 0.46f, 0.54f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.65f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.65f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.36f, 0.72f, 1.0f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.18f, 0.38f, 0.68f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.48f, 0.82f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.56f, 0.92f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.38f, 0.68f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.23f, 0.48f, 0.82f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.56f, 0.92f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.13f, 0.15f, 0.19f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.42f, 0.72f, 1.0f);
    c[ImGuiCol_TabActive] = ImVec4(0.18f, 0.38f, 0.68f, 1.0f);
    c[ImGuiCol_DockingPreview] = ImVec4(0.30f, 0.65f, 1.0f, 0.40f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.65f, 1.0f, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0.05f, 0.06f, 0.08f, 1.0f);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCmd) {
    const std::filesystem::path baseDir = executableDir();
    std::error_code ec;
    std::filesystem::create_directories(baseDir / "config", ec);
    std::filesystem::create_directories(baseDir / "logs", ec);
    std::filesystem::create_directories(baseDir / "projects", ec);

    m2rig::Logger::instance().init((baseDir / "logs" / "metin2_rigging_studio.log").string());
    m2rig::Logger::instance().info("Metin2 Rigging Studio starting.", "app");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"Metin2RiggingStudio";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Metin2 Rigging Studio", WS_OVERLAPPEDWINDOW,
                                100, 100, 1600, 950, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        m2rig::Logger::instance().fatal("CreateWindowEx failed.", "app");
        return 1;
    }

    std::string rendererError;
    if (!g_renderer.init(hwnd, 1600, 950, rendererError)) {
        m2rig::Logger::instance().fatal("Renderer init failed: " + rendererError, "app");
        MessageBoxA(hwnd, rendererError.c_str(), "Metin2 Rigging Studio — renderer error",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;  // portable: keep layout default per session (persisted wave 8)
    io.LogFilename = nullptr;
    applyDarkTheme();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(static_cast<struct ID3D11Device*>(g_renderer.deviceForBackend()),
                        static_cast<struct ID3D11DeviceContext*>(g_renderer.contextForBackend()));

    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);
    m2rig::setMainWindowHandle(hwnd);

    m2rig::App app;
    if (auto r = app.loadSampleArmor(); !r) {
        app.setStatus("Failed to build sample scene: " + r.error().message, "error");
    }
    app.runValidation();

    // Crash recovery: a leftover session.lock means the previous run died.
    const std::filesystem::path projectsDir = baseDir / "projects";
    const std::filesystem::path lockFile = projectsDir / "session.lock";
    const std::filesystem::path autoFile = projectsDir / "autosave.m2rig";
    bool showRecovery = false;
    {
        std::error_code lockEc;
        if (std::filesystem::exists(lockFile, lockEc) &&
            std::filesystem::exists(autoFile, lockEc))
            showRecovery = true;
        std::ofstream lock(lockFile);
        if (lock.is_open()) lock << GetCurrentProcessId() << "\n";
    }

    using Clock = std::chrono::steady_clock;
    auto lastFrame = Clock::now();
    double fpsAccum = 0.0;
    int fpsFrames = 0;

    bool running = true;
    MSG msg{};
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        if (g_resizePending && g_resizeW > 0 && g_resizeH > 0) {
            g_renderer.resize(g_resizeW, g_resizeH);
            g_resizePending = false;
        }

        const auto frameStart = Clock::now();
        const double dt =
            std::chrono::duration<double>(frameStart - lastFrame).count();
        lastFrame = frameStart;
        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 0.5) {
            app.fps = fpsFrames / fpsAccum;
            fpsAccum = 0.0;
            fpsFrames = 0;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        m2rig::ViewportRect viewportRect;
        m2rig::drawAllPanels(app, g_renderer, viewportRect, projectsDir, showRecovery);
        ImGui::EndFrame();

        // 3D scene renders behind the ImGui draw data, clipped to the
        // viewport panel rect.
        m2rig::renderScene(app, g_renderer, viewportRect);
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_renderer.present(app.vsync);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_renderer.shutdown();
    m2rig::Logger::instance().info("Metin2 Rigging Studio exiting.", "app");
    m2rig::Logger::instance().shutdown();
    {
        // Clean exit: remove the session lock so no recovery prompt appears.
        std::error_code byeEc;
        std::filesystem::remove(lockFile, byeEc);
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    return 0;
}
