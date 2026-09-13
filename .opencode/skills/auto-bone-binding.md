# Skill: Automatic Bone Binding (Heat Diffusion / Voxel)

## Description
Automatic bone binding using heat diffusion on tetrahedral mesh or voxel grid for natural weight assignment.

## File: `C:\rigapp\RigApp\Core\Skinning\AutoBoneBinding.cs`

## Algorithm: Heat Diffusion Binding

### 1. Tetrahedral Mesh Generation:
```csharp
public static class TetrahedralMesh
{
    public static (Vector3[] vertices, int[] tetrahedra) Generate(MeshData mesh, float cellSize = 0.05f)
    {
        // Create bounding box
        var bounds = ComputeBounds(mesh.Vertices);
        
        // Voxelize
        var voxels = Voxelize(bounds, cellSize);
        
        // Delaunay tetrahedralization (use TetGen or custom)
        // For simplicity: constrained Delaunay on surface + interior points
        
        return (vertices, tetrahedra);
    }
}
```

### 2. Heat Diffusion:
```csharp
public static class HeatDiffusionBinding
{
    public static float[,] ComputeWeights(MeshData mesh, float diffusionTime = 0.1f, int iterations = 100)
    {
        // Build tetrahedral mesh around model
        var (tetVertices, tetrahedra) = TetrahedralMesh.Generate(mesh);
        
        // Build Laplacian matrix (sparse)
        var laplacian = BuildLaplacian(tetVertices, tetrahedra);
        
        // For each bone, set boundary conditions (heat source at bone location)
        int boneCount = mesh.BoneCount;
        int vertexCount = tetVertices.Length;
        float[,] weights = new float[vertexCount, boneCount];
        
        for (int b = 0; b < boneCount; b++)
        {
            // Boundary condition: bone vertices = 1, others = 0
            var b = new float[vertexCount];
            SetBoneBoundaryCondition(b, mesh, tetVertices, b);
            
            // Solve heat equation: ∂u/∂t = Δu
            // Implicit Euler: (I - dt*L) * u_new = u_old
            var solver = new SparseLinearSolver(laplacian);
            float[] u = b;
            
            for (int iter = 0; iter < iterations; iter++)
            {
                u = solver.Solve(u); // (I - dt*L) * u_new = u_old
                EnforceBoundaryConditions(u, mesh, tetVertices, b);
            }
            
            for (int v = 0; v < vertexCount; v++)
                weights[v, b] = Math.Max(0, u[v]);
        }
        
        // Normalize per vertex
        for (int v = 0; v < vertexCount; v++)
        {
            float sum = 0;
            for (int b = 0; b < boneCount; b++) sum += weights[v, b];
            if (sum > 0) for (int b = 0; b < boneCount; b++) weights[v, b] /= sum;
        }
        
        // Sample at mesh vertices
        return SampleAtVertices(weights, tetVertices, mesh.Vertices);
    }
}
```

### 3. Voxel-Based Binding (GPU Accelerated):
```csharp
public static class VoxelBinding
{
    public static float[,] ComputeWeightsGPU(MeshData mesh, int resolution = 64)
    {
        // 1. Voxelize mesh into 3D texture
        // 2. For each bone, compute signed distance field (SDF) via GPU
        // 3. Weights = exp(-distance^2 / sigma^2) normalized
        // 4. Sample at mesh vertices
        
        // Requires ComputeShader (DX11/12)
        // Fallback to CPU if no GPU
    }
}
```

## Integration:
```csharp
public static class AutoBoneBinding
{
    public enum BindingMethod
    {
        HeatDiffusion,
        VoxelSDF,
        GeodesicVoxel
    }
    
    public static void BindMesh(MeshData mesh, BindingMethod method = BindingMethod.HeatDiffusion)
    {
        float[,] weights;
        
        switch (method)
        {
            case BindingMethod.HeatDiffusion:
                weights = HeatDiffusionBinding.ComputeWeights(mesh);
                break;
            case BindingMethod.VoxelSDF:
                weights = VoxelBinding.ComputeWeightsGPU(mesh);
                break;
            case BindingMethod.GeodesicVoxel:
                weights = GeodesicVoxelBinding.ComputeWeights(mesh);
                break;
        }
        
        // Apply to mesh (top 4 influences per vertex)
        ApplyWeightsToMesh(mesh, weights);
    }
}
```

## UI Integration:
- "Auto Bind" button in Weight Painter
- Method selector dropdown
- Parameters panel (diffusion time, voxel resolution, etc.)
- Preview before apply
- Progress bar with cancellation

## Verification:
- Produces valid weights (sum=1, max 4 influences)
- Natural deformations on test animations
- Performance: CPU < 5s for 50k verts, GPU < 500ms
- Works on all 182 model types