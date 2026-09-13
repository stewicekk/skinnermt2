# Skill: Professional HelixViewport3D Control

## Description
Create a reusable Viewport3D control with all professional features for Metin2 model viewing.

## File: C:\rigapp\RigApp\Controls\Viewport3D.xaml (+ .xaml.cs)

## Features:

### View Modes (Toolbar buttons):
1. **Solid** - Shaded with lighting
2. **Wireframe** - Edge lines only
3. **Bones** - Skeleton hierarchy (spheres + cylinders)
4. **Weights** - Vertex weight heatmap (per bone)
5. **WeightHeatmap** - Selected bone influence visualization

### Camera Presets (Toolbar dropdown):
- Front (0, 0, -1)
- Back (0, 0, 1)
- Left (-1, 0, 0)
- Right (1, 0, 0)
- Top (0, 1, 0)
- Bottom (0, -1, 0)
- Perspective (1, 1, -1)
- Isometric (1, 1, -1) orthographic

### Rendering:
- HelixViewport3D with DefaultLights
- GridLinesVisual3D (configurable size/spacing)
- Coordinate axes indicator (corner)
- FPS counter (top-right)
- Anti-aliasing (MSAA 4x)

### Bone Visualization:
- Bone spheres at joints (radius = 0.02 × bone length)
- Bone cylinders connecting joints
- Selected bone: highlight color + larger sphere
- Hover bone: preview highlight
- Bone labels (optional, 3D text)

### Weight Heatmap:
- Vertex colors based on bone influence
- Gradient: 0=blue → 0.5=green → 1=red
- Per-bone toggle (show only selected bone weights)
- Real-time update on weight changes

### Interaction:
- Trackball rotation (right mouse)
- Pan (middle mouse / shift+right)
- Zoom (wheel)
- Box selection (left drag)
- Bone picking (click on sphere/cylinder)
- Vertex picking (show weights in inspector)

### Model Loading:
```csharp
public void LoadModel(MeshData mesh, string skeletonPath = null)
{
    Clear();
    CreateMeshVisual3D(mesh);
    if (skeletonPath != null) LoadSkeleton(skeletonPath);
    FitCameraToModel();
}
```

### Public API:
- `SetViewMode(ViewMode mode)`
- `SetCameraPreset(CameraPreset preset)`
- `SelectBone(int boneIndex)`
- `SetWeightHeatmapBone(int boneIndex)`
- `ExportScreenshot(string path)`

## ViewModel (Viewport3DViewModel.cs):
- CurrentViewMode, CurrentCameraPreset
- SelectedBoneIndex, HoveredBoneIndex
- ShowGrid, ShowAxes, ShowBoneLabels
- Fps, TriangleCount, VertexCount

## Verification:
- Loads 182 GR2/FBX models at 60fps
- All view modes work
- Camera presets snap correctly
- Bone selection syncs with TreeView
- Weight heatmap updates in real-time