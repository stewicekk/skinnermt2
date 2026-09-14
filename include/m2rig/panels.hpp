#pragma once
// ImGui panel entry points (implemented in src/app/panels.cpp).
#include <filesystem>

namespace m2rig {

struct App;
class Renderer;

struct ViewportRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool valid = false;
};

ViewportRect drawViewportPanel(App& app);
void drawAllPanels(App& app, Renderer& renderer, ViewportRect& outViewport,
                   const std::filesystem::path& projectsDir, bool& showRecovery);
void renderScene(App& app, Renderer& renderer, const ViewportRect& rect);

// Native parent window for file dialogs (set once by main.cpp).
void setMainWindowHandle(void* hwnd);

}  // namespace m2rig
