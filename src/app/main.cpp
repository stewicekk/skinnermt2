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

// Wave 35: dark theme lives in the exe-local theme module (token-driven,
// see src/app/theme.hpp); main.cpp only calls it.
#include "theme.hpp"

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
    const std::string imguiIniPath = (baseDir / "config" / "imgui.ini").string();
    io.IniFilename = imguiIniPath.c_str();
    io.LogFilename = nullptr;
    m2rig::theme::applyTheme(m2rig::ThemeVariant::Dark);  // default until prefs load
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(static_cast<struct ID3D11Device*>(g_renderer.deviceForBackend()),
                        static_cast<struct ID3D11DeviceContext*>(g_renderer.contextForBackend()));

    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);
    m2rig::setMainWindowHandle(hwnd);

    m2rig::App app;
    // Load user preferences
    app.loadPreferences(baseDir / "config");
    // Apply the persisted theme variant (overrides the startup default).
    m2rig::theme::applyTheme(static_cast<m2rig::ThemeVariant>(app.uiSettings.themeVariant));
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
            if (g_renderer.resize(g_resizeW, g_resizeH)) {
                g_resizePending = false;
            } else {
                m2rig::Logger::instance().warning(
                    "D3D11 resize failed; retaining the pending resize request.", "app");
            }
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

        // 3D scene is now rendered inside drawViewportPanel to an offscreen texture,
        // composited via ImGui::Image(). No separate renderScene call needed.
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_renderer.present(app.vsync);
    }

    // Save user preferences on exit
    app.savePreferences(baseDir / "config");

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
