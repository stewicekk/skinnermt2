// G4 routing matrix test (headless): pins resolveDrawPath, the pure
// ViewMode x usePbr x wantTextured x hasTexture x skinned routing table that
// drawSceneContents (src/app/panels.cpp) dispatches on. resolveDrawPath lives
// in m2rig_core (declared in include/m2rig/app.hpp, defined in
// src/app_state.cpp) so this links with no exe-only symbols.
#include <cstdio>
#include <filesystem>

#include "expect.hpp"
#include "m2rig/app.hpp"

using namespace m2rig;

// Independent re-derivation of the documented contract (not a copy of the
// implementation's control flow): debug ramps first, Wireframe second,
// solid-mode PBR/texture combinations last.
static DrawPath expectedDrawPath(ViewMode mode, bool usePbr, bool wantTextured) {
    switch (mode) {
        case ViewMode::Normals:
        case ViewMode::Height:
        case ViewMode::Weights:
        case ViewMode::UV:
            return DrawPath::FlatDebug;
        case ViewMode::Wireframe:
            return DrawPath::WireBlinn;
        case ViewMode::Solid:
        case ViewMode::SolidWireframe:
            break;
    }
    if (usePbr && wantTextured) return DrawPath::PbrTextured;
    if (usePbr) return DrawPath::PbrSolid;
    if (wantTextured) return DrawPath::SolidTextured;
    return DrawPath::SolidUntextured;
}

M2RIG_TEST(app, resolve_draw_path_routing_matrix) {
    int failures = 0;
    // Full matrix: 7 modes x usePbr x wantTextured x hasTexture x skinned.
    // hasTexture and skinned are orthogonal by contract (renderer fallback /
    // Skinned draw suffix), so they must never change the resolved path.
    for (int m = 0; m < 7; ++m) {
        const auto mode = static_cast<ViewMode>(m);
        for (int p = 0; p < 2; ++p) {
            for (int t = 0; t < 2; ++t) {
                const DrawPath want = expectedDrawPath(mode, p != 0, t != 0);
                for (int h = 0; h < 2; ++h) {
                    for (int s = 0; s < 2; ++s) {
                        const DrawPath got = resolveDrawPath(mode, p != 0, t != 0, h != 0, s != 0);
                        if (!(got == want)) {
                            printf("    FAIL app.resolve_draw_path_routing_matrix: mode=%d pbr=%d "
                                   "textured=%d hasTexture=%d skinned=%d: got %s, want %s\n",
                                   m, p, t, h, s, drawPathName(got), drawPathName(want));
                            ++failures;
                        }
                    }
                }
            }
        }
    }
    // Names stay valid for every enumerator (failure output readability).
    CHECK_TRUE(drawPathName(DrawPath::SolidUntextured) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::SolidTextured) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::FlatDebug) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::WireBlinn) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::PbrSolid) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::PbrTextured) != nullptr);
    return failures;
}

// Wave 37: mseTabEnabled must round-trip through user_prefs.json like every
// other UI flag (it lived in a panels_msm.cpp global before and was never
// persisted). Save/load are App methods in the core (src/app_state.cpp).
M2RIG_TEST(app, prefs_round_trip_mse_tab_enabled) {
    int failures = 0;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "m2rig_test_prefs_mse";
    fs::remove_all(dir, ec);
    // Flipped flag persists...
    {
        App a;
        a.uiSettings.mseTabEnabled = false;
        const ResultVoid saved = a.savePreferences(dir);
        CHECK_TRUE(saved.succeeded());
    }
    // ...and loads back into a fresh App (default true).
    {
        App b;
        CHECK_TRUE(b.uiSettings.mseTabEnabled);
        const ResultVoid loaded = b.loadPreferences(dir);
        CHECK_TRUE(loaded.succeeded());
        CHECK_FALSE(b.uiSettings.mseTabEnabled);
    }
    // Default (true) round-trips too: flipped receiver recovers the file value.
    {
        App c;
        CHECK_TRUE(c.savePreferences(dir).succeeded());
        App d;
        d.uiSettings.mseTabEnabled = false;
        CHECK_TRUE(d.loadPreferences(dir).succeeded());
        CHECK_TRUE(d.uiSettings.mseTabEnabled);
    }
    fs::remove_all(dir, ec);
    return failures;
}
