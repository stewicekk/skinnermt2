#pragma once
// ImGui panel entry points (implemented in src/app/panels.cpp).
#include <filesystem>

#include "m2rig/math.hpp"

namespace m2rig {

struct App;
class Renderer;

struct ViewportRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool valid = false;
};

ViewportRect drawViewportPanel(App& app, Renderer& renderer);
void drawAllPanels(App& app, Renderer& renderer, ViewportRect& outViewport,
                   const std::filesystem::path& projectsDir, bool& showRecovery);
void renderScene(App& app, Renderer& renderer, const ViewportRect& rect);
// Wave 21: offscreen scene contents shared by the backbuffer smoke path and
// the dock-proof viewport-texture path. Returns draws issued (mesh+overlay+
// lines); never claims pixels without draws.
int drawSceneContents(App& app, Renderer& renderer, const Mat4& viewProj, const ViewportRect& rect,
                      float modelRadius);

// Native parent window for file dialogs (set once by main.cpp).
void setMainWindowHandle(void* hwnd);
void drawSettingsPanel(App& app);
void drawBoneDisplayPanel(App& app);
void drawGizmoPanel(App& app);
void drawViewportSettingsPanel(App& app);

}  // namespace m2rig
