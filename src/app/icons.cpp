// Vector UI icons (Wave 44): draw-list glyphs + the button / tab surfaces that
// embed them. See icons.hpp for why draw-list (not font / texture / emoji).

#include "icons.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace m2rig {
namespace {

// Grid context: all icon geometry is authored on a 16x16 grid (y down) and
// scaled to `size` px around `center`. Stroke ~1.6 grid units stays crisp.
struct Ctx {
    ImDrawList* dl;
    ImVec2 c;
    float scale;  // size / 16
    float thick;  // stroke width in px
    ImU32 col;
    ImVec2 P(float x, float y) const { return ImVec2(c.x + (x - 8.0f) * scale, c.y + (y - 8.0f) * scale); }
    void line(float x1, float y1, float x2, float y2) { dl->AddLine(P(x1, y1), P(x2, y2), col, thick); }
    void rect(float x1, float y1, float x2, float y2) { dl->AddRect(P(x1, y1), P(x2, y2), col, 0.0f, thick); }
    void rectF(float x1, float y1, float x2, float y2) { dl->AddRectFilled(P(x1, y1), P(x2, y2), col); }
    void circle(float x, float y, float r) { dl->AddCircle(P(x, y), r * scale, col, 0, thick); }
    void circleF(float x, float y, float r) { dl->AddCircleFilled(P(x, y), r * scale, col, 0); }
    void triF(float x1, float y1, float x2, float y2, float x3, float y3) {
        dl->AddTriangleFilled(P(x1, y1), P(x2, y2), P(x3, y3), col);
    }
    void poly(const std::vector<ImVec2>& pts) {
        if (pts.size() < 2) return;
        // AddPolyline expects const ImVec2*; build a screen-space copy.
        std::vector<ImVec2> sp;
        sp.reserve(pts.size());
        for (const ImVec2& p : pts) sp.push_back(P(p.x, p.y));
        dl->AddPolyline(sp.data(), static_cast<int>(sp.size()), col, 0, thick);
    }
    // Clockwise arc (y down) from a0 to a1 radians, as a polyline.
    void arc(float cx, float cy, float r, float a0, float a1, int n = 24) {
        std::vector<ImVec2> pts;
        pts.reserve(static_cast<std::size_t>(n) + 1);
        for (int i = 0; i <= n; ++i) {
            const float a = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(n);
            pts.push_back(ImVec2(cx + r * std::cos(a), cy + r * std::sin(a)));
        }
        poly(pts);
    }
    // Arrowhead at grid point (tx,ty) pointing along unit (dx,dy).
    // Component arithmetic (no IMGUI_DEFINE_MATH_OPERATORS in this TU).
    void arrow(float tx, float ty, float dx, float dy, float len) {
        const float il = 1.0f / (std::sqrt(dx * dx + dy * dy) + 1e-6f);
        dx *= il;
        dy *= il;
        const ImVec2 tip = P(tx, ty);
        const ImVec2 base = P(tx - dx * len, ty - dy * len);
        const float px = -dy * len * 0.5f * scale;
        const float py = dx * len * 0.5f * scale;
        dl->AddLine(tip, ImVec2(base.x + px, base.y + py), col, thick);
        dl->AddLine(tip, ImVec2(base.x - px, base.y - py), col, thick);
    }
};

void drawNew(Ctx& x) {
    x.rect(2.5f, 1.5f, 13.5f, 14.5f);
    x.line(8, 5.5f, 8, 10.5f);
    x.line(5.5f, 8, 10.5f, 8);
}
void drawOpen(Ctx& x) {
    x.poly({ImVec2(1.5f, 4.5f), ImVec2(1.5f, 2.5f), ImVec2(6, 2.5f), ImVec2(8, 4.5f),
            ImVec2(14.5f, 4.5f), ImVec2(14.5f, 12.5f), ImVec2(1.5f, 12.5f), ImVec2(1.5f, 4.5f)});
}
void drawSave(Ctx& x) {
    x.rect(3, 1.5f, 13, 14.5f);
    x.rect(5.5f, 1.5f, 10.5f, 5.5f);
    x.rect(5, 9.5f, 11, 14);
}
void drawUndo(Ctx& x) {
    x.arc(8, 8, 5.5f, -0.35f, 3.5f);  // east -> south -> west
    x.arrow(2.8f, 6.1f, -1, 0, 2.2f);
}
void drawRedo(Ctx& x) {
    x.arc(8, 8, 5.5f, 3.5f, 6.65f);  // west -> north -> east
    x.arrow(13.2f, 9.9f, 1, 0, 2.2f);
}
void drawPlay(Ctx& x) { x.triF(4.5f, 3.5f, 4.5f, 12.5f, 13, 8); }
void drawPause(Ctx& x) {
    x.rectF(4.5f, 3.5f, 7, 12.5f);
    x.rectF(9, 3.5f, 11.5f, 12.5f);
}
void drawFrame(Ctx& x) {
    x.line(2, 6, 2, 2);
    x.line(2, 2, 6, 2);
    x.line(10, 2, 14, 2);
    x.line(14, 2, 14, 6);
    x.line(14, 10, 14, 14);
    x.line(14, 14, 10, 14);
    x.line(6, 14, 2, 14);
    x.line(2, 14, 2, 10);
}
void drawProjection(Ctx& x) {
    x.rect(2.5f, 4.5f, 13.5f, 12.5f);
    x.rect(5.5f, 7, 10.5f, 11);
    x.line(2.5f, 4.5f, 5.5f, 7);
    x.line(13.5f, 4.5f, 10.5f, 7);
    x.line(2.5f, 12.5f, 5.5f, 11);
    x.line(13.5f, 12.5f, 10.5f, 11);
}
void drawFront(Ctx& x) {
    x.rect(2.5f, 2.5f, 13.5f, 13.5f);
    x.circleF(8, 8, 1.8f);
}
void drawBack(Ctx& x) {
    x.rect(2.5f, 2.5f, 13.5f, 13.5f);
    x.circle(8, 8, 2.2f);
}
void drawTop(Ctx& x) {
    x.rect(2.5f, 2.5f, 13.5f, 13.5f);
    x.poly({ImVec2(5, 9.5f), ImVec2(8, 6.3f), ImVec2(11, 9.5f)});
}
void drawBottom(Ctx& x) {
    x.rect(2.5f, 2.5f, 13.5f, 13.5f);
    x.poly({ImVec2(5, 6.5f), ImVec2(8, 9.7f), ImVec2(11, 6.5f)});
}
void drawLeft(Ctx& x) {
    x.rect(2.5f, 2.5f, 13.5f, 13.5f);
    x.poly({ImVec2(9.5f, 5), ImVec2(6.3f, 8), ImVec2(9.5f, 11)});
}
void drawRight(Ctx& x) {
    x.rect(2.5f, 2.5f, 13.5f, 13.5f);
    x.poly({ImVec2(6.5f, 5), ImVec2(9.7f, 8), ImVec2(6.5f, 11)});
}
void drawShading(Ctx& x) {
    x.circle(8, 8, 5.5f);
    // Filled left half (light/shadow split).
    x.dl->PathClear();
    x.dl->PathArcTo(x.P(8, 8), 5.5f * x.scale, 1.5708f, 4.7124f);
    x.dl->PathFillConvex(x.col);
}
void drawAssets(Ctx& x) {
    x.rect(3, 6, 13, 14);
    x.poly({ImVec2(3, 6), ImVec2(6, 3), ImVec2(16, 3), ImVec2(13, 6), ImVec2(3, 6)});
    x.poly({ImVec2(13, 6), ImVec2(16, 3), ImVec2(16, 11), ImVec2(13, 14), ImVec2(13, 6)});
}
void drawScene(Ctx& x) {
    x.rect(2, 3, 14, 13);
    x.poly({ImVec2(2, 13), ImVec2(7, 7), ImVec2(10, 10), ImVec2(13, 6), ImVec2(14, 7), ImVec2(14, 13),
            ImVec2(2, 13)});
    x.circle(11, 4.5f, 1.3f);
}
void drawSkeleton(Ctx& x) {
    x.circle(4, 4, 1.8f);
    x.circle(12, 4, 1.8f);
    x.circle(8, 12, 1.8f);
    x.line(4, 4, 8, 12);
    x.line(12, 4, 8, 12);
}
void drawBone(Ctx& x) {
    x.line(4, 12, 12, 4);
    x.circle(4, 12, 2);
    x.circle(12, 4, 2);
    x.circle(3, 13, 0.9f);
    x.circle(13, 3, 0.9f);
}
void drawWeights(Ctx& x) {
    x.rect(2, 4, 14, 12);
    x.rectF(3.5f, 9.5f, 5.1f, 12);
    x.rectF(6.3f, 8, 7.9f, 12);
    x.rectF(9.1f, 6.5f, 10.7f, 12);
    x.rectF(11.9f, 5, 13.5f, 12);
}
void drawMaterials(Ctx& x) {
    x.circle(8, 8, 5.5f);
    x.circle(6, 5.8f, 1.4f);
}
void drawBoneDisplay(Ctx& x) {
    x.circle(8, 8, 4.5f);
    x.line(8, 1.5f, 8, 4);
    x.line(8, 12, 8, 14.5f);
    x.line(1.5f, 8, 4, 8);
    x.line(12, 8, 14.5f, 8);
}
void drawGizmo(Ctx& x) {
    x.line(8, 8, 14, 8);
    x.line(8, 8, 8, 2);
    x.line(8, 8, 3, 13);
    x.arrow(14, 8, 1, 0, 2);
    x.arrow(8, 2, 0, -1, 2);
    x.arrow(3, 13, -0.5f, 1, 2);
}
void drawViewportSettings(Ctx& x) {
    x.rect(2, 3, 14, 13);
    x.line(2, 6.5f, 14, 6.5f);
    x.circleF(4, 4.75f, 0.6f);
    x.circleF(5.8f, 4.75f, 0.6f);
}
void drawSettings(Ctx& x) {
    x.circle(8, 8, 3.2f);
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * 0.7854f;
        const float ca = std::cos(a);
        const float sa = std::sin(a);
        x.line(8 + 4.6f * ca, 8 + 4.6f * sa, 8 + 6.2f * ca, 8 + 6.2f * sa);
    }
}
void drawExport(Ctx& x) {
    x.rect(3, 6, 13, 13);
    x.line(8, 11, 8, 2.5f);
    x.poly({ImVec2(5, 5.5f), ImVec2(8, 2.5f), ImVec2(11, 5.5f)});
}
void drawProject(Ctx& x) {
    x.rect(2.5f, 5.5f, 13.5f, 13);
    x.poly({ImVec2(6, 5.5f), ImVec2(6, 2.5f), ImVec2(10, 2.5f), ImVec2(10, 5.5f)});
    x.line(2.5f, 9, 13.5f, 9);
}
void drawValidation(Ctx& x) {
    x.circle(8, 8, 6);
    x.poly({ImVec2(5, 8.3f), ImVec2(7.3f, 10.6f), ImVec2(11.2f, 5.2f)});
}
void drawConsole(Ctx& x) {
    x.rect(2, 3, 14, 13);
    x.poly({ImVec2(4.5f, 6), ImVec2(7.5f, 9), ImVec2(4.5f, 12)});
    x.line(9, 12, 12.5f, 12);
}
void drawSystem(Ctx& x) {
    x.rect(2, 3, 14, 11);
    x.line(8, 11, 8, 13.5f);
    x.line(5, 13.5f, 11, 13.5f);
}
void drawMsmInspector(Ctx& x) {
    x.rect(3, 1.5f, 10, 14.5f);
    x.circle(10.5f, 10.5f, 3);
    x.line(12.7f, 12.7f, 15, 15);
}
void drawMseEffects(Ctx& x) {
    x.poly({ImVec2(8, 1.5f), ImVec2(9.2f, 6.8f), ImVec2(14.5f, 8), ImVec2(9.2f, 9.2f), ImVec2(8, 14.5f),
            ImVec2(6.8f, 9.2f), ImVec2(1.5f, 8), ImVec2(6.8f, 6.8f), ImVec2(8, 1.5f)});
}
void drawMove(Ctx& x) {
    x.line(8, 2.5f, 8, 5.5f);
    x.line(8, 10.5f, 8, 13.5f);
    x.line(2.5f, 8, 5.5f, 8);
    x.line(10.5f, 8, 13.5f, 8);
    x.arrow(8, 2.5f, 0, -1, 1.8f);
    x.arrow(8, 13.5f, 0, 1, 1.8f);
    x.arrow(2.5f, 8, -1, 0, 1.8f);
    x.arrow(13.5f, 8, 1, 0, 1.8f);
}
void drawRotate(Ctx& x) {
    x.arc(8, 8, 5, 0.35f, 5.9f);  // near-full circle
    x.arrow(11.5f, 4.5f, 0.7f, -0.7f, 2);
}
void drawScale(Ctx& x) {
    x.line(4.5f, 11.5f, 11.5f, 4.5f);
    x.arrow(11.5f, 4.5f, 0.7f, -0.7f, 2);
    x.arrow(4.5f, 11.5f, -0.7f, 0.7f, 2);
}
void drawSnap(Ctx& x) {
    x.rect(3.5f, 2, 6.5f, 5.5f);
    x.rect(9.5f, 2, 12.5f, 5.5f);
    x.line(3.5f, 5.5f, 3.5f, 9);
    x.line(12.5f, 5.5f, 12.5f, 9);
    x.arc(8, 9, 4.5f, 3.1416f, 6.2832f);  // bottom U
}
void drawGrid(Ctx& x) {
    x.line(5.3f, 2, 5.3f, 14);
    x.line(10.7f, 2, 10.7f, 14);
    x.line(2, 5.3f, 14, 5.3f);
    x.line(2, 10.7f, 14, 10.7f);
}
void drawCamera(Ctx& x) {
    x.rect(2, 5, 14, 12);
    x.circle(8, 8.5f, 2.5f);
    x.poly({ImVec2(5, 5), ImVec2(5, 3), ImVec2(9, 3), ImVec2(9, 5)});
}
void drawMesh(Ctx& x) {
    x.poly({ImVec2(2.5f, 12.5f), ImVec2(8, 3.5f), ImVec2(13.5f, 12.5f), ImVec2(2.5f, 12.5f)});
    x.line(8, 9, 2.5f, 12.5f);
    x.line(8, 9, 8, 3.5f);
    x.line(8, 9, 13.5f, 12.5f);
}
void drawStatusOk(Ctx& x) { x.poly({ImVec2(3, 8.5f), ImVec2(6.8f, 12.3f), ImVec2(13, 4.2f)}); }
void drawStatusWarn(Ctx& x) {
    x.poly({ImVec2(8, 2.5f), ImVec2(14, 13), ImVec2(2, 13), ImVec2(8, 2.5f)});
    x.line(8, 7, 8, 9.8f);
    x.circleF(8, 11.5f, 0.5f);
}
void drawStatusErr(Ctx& x) {
    x.circle(8, 8, 6);
    x.line(5.8f, 5.8f, 10.2f, 10.2f);
    x.line(10.2f, 5.8f, 5.8f, 10.2f);
}
void drawStatusInfo(Ctx& x) {
    x.circle(8, 8, 6);
    x.line(8, 5.8f, 8, 8.8f);
    x.circleF(8, 11, 0.5f);
}

using IconFn = void (*)(Ctx&);

}  // namespace

void drawIcon(ImDrawList* dl, UiIcon icon, ImVec2 center, float size, ImU32 color) {
    if (icon <= UiIcon::None || icon >= UiIcon::Count || size <= 0.0f) return;
    static const IconFn kFns[static_cast<int>(UiIcon::Count)] = {
        nullptr,            // None
        drawNew,            // New
        drawOpen,           // Open
        drawSave,           // Save
        drawUndo,           // Undo
        drawRedo,           // Redo
        drawPlay,           // Play
        drawPause,          // Pause
        drawFrame,          // Frame
        drawProjection,     // Projection
        drawFront,          // Front
        drawBack,           // Back
        drawTop,            // Top
        drawBottom,         // Bottom
        drawLeft,           // Left
        drawRight,          // Right
        drawShading,        // Shading
        drawAssets,         // Assets
        drawScene,          // Scene
        drawSkeleton,       // Skeleton
        drawBone,           // Bone
        drawWeights,        // Weights
        drawMaterials,      // Materials
        drawBoneDisplay,    // BoneDisplay
        drawGizmo,          // Gizmo
        drawViewportSettings, // ViewportSettings
        drawSettings,       // Settings
        drawExport,         // Export
        drawProject,        // Project
        drawValidation,     // Validation
        drawConsole,        // Console
        drawSystem,         // System
        drawMsmInspector,   // MsmInspector
        drawMseEffects,     // MseEffects
        drawMove,           // Move
        drawRotate,         // Rotate
        drawScale,          // Scale
        drawSnap,           // Snap
        drawGrid,           // Grid
        drawCamera,         // Camera
        drawMesh,           // Mesh
        drawStatusOk,       // StatusOk
        drawStatusWarn,     // StatusWarn
        drawStatusErr,      // StatusErr
        drawStatusInfo,     // StatusInfo
    };
    Ctx x{dl, center, size / 16.0f, size / 16.0f * 1.6f, color};
    kFns[static_cast<int>(icon)](x);
}

bool iconButton(UiIcon icon, const char* label, bool disabled) {
    const char* text = label ? label : iconLabel(icon);
    const float iconSize = ImGui::GetTextLineHeight() - 2.0f;
    const float gap = iconSize * 0.35f;
    const float spaceW = ImGui::CalcTextSize(" ").x;
    const float contentW = iconSize + gap + ImGui::CalcTextSize(text).x;
    const int spaces = (int)std::ceil(contentW / spaceW);
    const std::string pad(static_cast<std::size_t>(spaces), ' ');
    ImGui::PushID(static_cast<int>(icon));
    ImGui::BeginDisabled(disabled);
    const bool pressed = ImGui::Button(pad.c_str());
    ImGui::EndDisabled();
    ImGui::PopID();
    // Draw glyph + label over the (invisible) space run, inside the button.
    const ImVec2 bbMin = ImGui::GetItemRectMin();
    const ImVec2 bbMax = ImGui::GetItemRectMax();
    const float bbH = bbMax.y - bbMin.y;
    const ImVec2 iconCenter(bbMin.x + ImGui::GetStyle().FramePadding.x + iconSize * 0.5f,
                            bbMin.y + bbH * 0.5f);
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    drawIcon(ImGui::GetWindowDrawList(), icon, iconCenter, iconSize, col);
    const ImVec2 textPos(bbMin.x + ImGui::GetStyle().FramePadding.x + iconSize + gap,
                         bbMin.y + (bbH - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(textPos, col, text);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const char* tip = iconTooltip(icon);
        if (tip && *tip) ImGui::SetTooltip("%s", tip);
    }
    return pressed;
}

bool iconTabItem(UiIcon icon, const char* label, bool* p_open, ImGuiTabItemFlags flags) {
    const char* text = label ? label : iconLabel(icon);
    const float iconSize = ImGui::GetTextLineHeight() - 2.0f;
    const float gap = iconSize * 0.35f;
    const float spaceW = ImGui::CalcTextSize(" ").x;
    const float contentW = iconSize + gap + ImGui::CalcTextSize(text).x;
    const int spaces = (int)std::ceil(contentW / spaceW);
    // Only spaces go to BeginTabItem: the glyph + text are drawn by us at the
    // label slot. (Appending the text too would make ImGui draw it after the
    // spaces as well — double text.)
    std::string pad(static_cast<std::size_t>(spaces), ' ');
    if (!ImGui::BeginTabItem(pad.c_str(), p_open, flags)) return false;
    // ItemAdd ran inside BeginTabItem, so the tab rect is valid (unless this is
    // the tab's first frame, where the rect is empty — skip drawing then).
    const ImVec2 bbMin = ImGui::GetItemRectMin();
    const ImVec2 bbMax = ImGui::GetItemRectMax();
    if (bbMax.x > bbMin.x && bbMax.y > bbMin.y) {
        const float bbH = bbMax.y - bbMin.y;
        const ImVec2 iconCenter(bbMin.x + ImGui::GetStyle().FramePadding.x + iconSize * 0.5f,
                                bbMin.y + bbH * 0.5f);
        drawIcon(ImGui::GetWindowDrawList(), icon, iconCenter, iconSize,
                 ImGui::GetColorU32(ImGuiCol_Text));
        const ImVec2 textPos(bbMin.x + ImGui::GetStyle().FramePadding.x + iconSize + gap,
                             bbMin.y + (bbH - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), text);
    }
    return true;
}

}  // namespace m2rig
