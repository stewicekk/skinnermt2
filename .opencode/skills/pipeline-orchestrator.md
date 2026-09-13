# Skill: Visual Pipeline Orchestrator

## Description
Create a visual node-based pipeline editor for the RigApp weight transfer pipeline.

## Files:
- `C:\rigapp\RigApp\Controls\PipelineGraph.xaml`
- `C:\rigapp\RigApp\Controls\PipelineGraph.xaml.cs`
- `C:\rigapp\RigApp\ViewModels\PipelineViewModel.cs`
- `C:\rigapp\RigApp\Core\Pipeline\PipelineEngine.cs`

## Pipeline Nodes (Steps):

### Input Nodes:
- **LoadGr2** → MeshData (uses Noesis)
- **LoadFbx** → MeshData (uses AssimpNet)
- **LoadNpz** → MeshData (ML weights)
- **LoadSkeleton** → MeshData (FBX skeleton)

### Processing Nodes:
- **BoneMapping** → BoneMap (source→target)
- **WeightTransfer** → MeshData (KNN/Heat/BBW/Voxel)
- **ConstraintEnforcer** → MeshData (Metin2 validation + auto-fix)
- **Symmetrize** → MeshData (left↔right)
- **SmoothWeights** → MeshData (Laplacian)
- **PruneWeights** → MeshData (threshold)
- **LimitInfluences** → MeshData (max 4)

### Output Nodes:
- **ExportFbx** → string (path)
- **ExportGr2** → string (path, via Noesis)
- **ExportSmd** → string (path, legacy)
- **ExportReport** → ValidationResult

### Utility Nodes:
- **Cache** → MeshData (disk/memory cache)
- **Branch** → conditional execution
- **Merge** → combine multiple meshes
- **Split** → separate by bone groups

## Visual Editor:
- Node canvas with pan/zoom
- Drag from palette to canvas
- Connect ports (type-safe: MeshData→MeshData, BoneMap→BoneMap)
- Node properties panel (right side)
- Execution order auto-calculated (topological sort)
- Run/Stop/Pause/Step buttons
- Node status: Idle/Running/Success/Error (color coded)
- Per-node timing display
- Checkpoint save/load (serialize pipeline + intermediate data)

## Pipeline Engine:
```csharp
public class PipelineEngine
{
    public event Action<PipelineNode, NodeStatus> NodeStatusChanged;
    public event Action<float> OverallProgressChanged;
    
    public async Task<PipelineResult> ExecuteAsync(PipelineGraph graph, CancellationToken ct = default)
    {
        var sorted = TopologicalSort(graph.Nodes);
        var context = new PipelineContext();
        
        foreach (var node in sorted)
        {
            ct.ThrowIfCancellationRequested();
            NodeStatusChanged?.Invoke(node, NodeStatus.Running);
            var sw = Stopwatch.StartNew();
            
            try
            {
                var outputs = await node.ExecuteAsync(context, ct);
                foreach (var (port, value) in outputs)
                    context.SetValue(port, value);
                
                node.Status = NodeStatus.Success;
                node.ExecutionTime = sw.Elapsed;
            }
            catch (Exception ex)
            {
                node.Status = NodeStatus.Error;
                node.Error = ex.Message;
                throw;
            }
            finally
            {
                NodeStatusChanged?.Invoke(node, node.Status);
            }
        }
        
        return new PipelineResult { Context = context, Success = true };
    }
}
```

## Presets:
- **FullArmorTransfer**: LoadGr2→LoadSkeleton→BoneMapping→WeightTransfer→ConstraintEnforcer→ExportGr2
- **WeightPainting**: LoadFbx→WeightTransfer→SmoothWeights→ConstraintEnforcer→ExportFbx
- **Batch182**: Loop over armor sets, execute FullArmorTransfer for each
- **MLTraining**: LoadNpz→LoadSkeleton→ExportFbx (for training data prep)

## Verification:
- Graph saves/loads as JSON
- All 182 models process via Batch182 preset
- Checkpoint resume works
- Visual feedback at 60fps
- Error nodes show details on click