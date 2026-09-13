# Skill: Real-time Weight Painting Visualizer

## Description
Create a professional weight painting tool integrated with the 3D viewport for precise weight editing.

## Files:
- `C:\rigapp\RigApp\Controls\WeightPainter.xaml`
- `C:\rigapp\RigApp\Controls\WeightPainter.xaml.cs`
- `C:\rigapp\RigApp\ViewModels\WeightPainterViewModel.cs`

## Features:

### Brush Tool:
- **Radius**: 0.01 - 1.0 (world units), slider + numeric input
- **Strength**: 0.01 - 1.0, slider
- **Falloff**: Linear, Smooth, Sharp, Constant (combobox)
- **Mode**: Add, Subtract, Smooth, Replace (radio buttons)
- **Symmetry**: X-mirror (left↔right bone pairs), toggle

### Visual Feedback:
- Brush preview circle on mesh surface (projected)
- Affected vertices highlight (real-time)
- Weight value tooltip on hover
- Stroke preview before commit

### Vertex Weight Inspector (dockable panel):
- Table: Vertex Index | Position | Bone Weights (4 cols) | Sum
- Filter: Selected vertices, Non-zero weights, Specific bone
- Sort by: Index, Weight sum, Distance to camera
- Edit: Double-click weight value to edit directly
- Copy/Paste rows (Ctrl+C/V)

### Bone Weight Operations:
- **Normalize Selected**: Sum = 1.0
- **Prune Small**: Remove weights < 0.01
- **Limit Influences**: Max 4 bones/vertex
- **Mirror Weights**: Left↔Right (by bone name mapping)
- **Smooth Weights**: Laplacian smoothing (iterations slider)
- **Clear Bone**: Set bone weight to 0 for selected vertices

### Selection Tools:
- **Brush Select**: Paint selection on mesh
- **Box Select**: Drag rectangle
- **Bone Select**: Click bone → select all vertices with weight > threshold
- **Grow/Shrink**: Expand/contract selection by connectivity
- **Invert**: Invert selection

### Integration with Viewport3D:
- Real-time vertex color update on weight change
- Selected bone highlight in viewport
- Weight heatmap for current brush bone
- Undo/Redo stack (50 steps)

### Keyboard Shortcuts:
- B: Brush mode
- S: Smooth mode
- R: Radius (scroll to adjust)
- Shift: Symmetry toggle
- Ctrl+Z/Y: Undo/Redo

## Data Structures:
```csharp
public class WeightStroke
{
    public List<int> VertexIndices;
    public List<float> OldWeights;
    public List<float> NewWeights;
    public int BoneIndex;
    public BrushMode Mode;
}
```

## Verification:
- 100k vertex mesh: brush stroke < 16ms
- Real-time vertex color update
- Undo/Redo works correctly
- Symmetry mirrors correctly
- Export weights match Metin2 constraints