# Skill: ImGui Theme System

The UI style lives in code, not XAML: `applyDarkTheme()` in
`src/app/main.cpp` (rounding, spacing, scrollbars, tabs, accent
blue/orange status colors). Viewport clear color matches in
`renderScene` (`src/app/panels.cpp`).

## Editing the theme
- Change `ImGuiStyle` + `ImGuiCol_*` values in `applyDarkTheme()` only;
  panels use relative styling (no hardcoded colors except status +
  heatmap ramp + bone colors in `boneSegments`).
- Weight heatmap ramp: `buildGpuVerticesWeight` (`src/mesh_views.cpp`,
  blue->cyan->green->yellow->red); legend drawn in `drawWeightPanel`.
- Bone colors: selected yellow, hover orange, `equip_*`/`stip` cyan
  (`boneSegments`, `src/app/panels.cpp`).

Legacy note: `C:\rigapp\RigApp\Theme.xaml` (WPF) does not exist here.
