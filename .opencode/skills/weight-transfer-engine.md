# Skill: Advanced Weight Transfer Engine

## Description
Implement multiple weight transfer algorithms with GPU acceleration for production-quality results.

## File: `C:\rigapp\RigApp\Core\WeightTransferEngine.cs`

## Algorithms:

### 1. KNN (K-Nearest Neighbors) - Existing, optimize:
```csharp
public static void TransferWeightsKnn(MeshData source, MeshData target, int k = 5, float maxDistance = float.MaxValue)
{
    // KD-Tree for O(log n) neighbor search
    var kdTree = new KDTree(source.Vertices);
    
    Parallel.For(0, target.VertexCount, i =>
    {
        var neighbors = kdTree.Query(target.GetVertex(i), k, maxDistance);
        // Weighted average of neighbor weights
        // Inverse distance weighting with falloff
    });
}
```

### 2. Heat Diffusion (Volumetric):
```csharp
public static void TransferWeightsHeatDiffusion(MeshData source, MeshData target, float diffusionTime = 0.1f, int iterations = 50)
{
    // Build tetrahedral mesh or voxel grid around target
    // Diffuse source weights through volume
    // Sample at target vertices
    // Better for topology mismatch
}
```

### 3. Bounded Biharmonic Weights (BBW):
```csharp
public static void TransferWeightsBBW(MeshData source, MeshData target, int maxBones = 4)
{
    // Solve Laplace equation with bone constraints
    // Produces smooth, natural deformations
    // Use sparse linear solver (SuiteSparse or custom)
}
```

### 4. Geodesic Voxel Binding (GPU):
```csharp
public static void TransferWeightsGeodesicVoxel(MeshData source, MeshData target, int voxelResolution = 64)
{
    // Voxelize both meshes
    // Compute geodesic distances in voxel space
    // Transfer weights via closest voxel correspondence
    // Implement via ComputeShader for GPU acceleration
}
```

## Unified API:
```csharp
public enum WeightTransferAlgorithm
{
    KNN,
    HeatDiffusion,
    BoundedBiharmonic,
    GeodesicVoxel
}

public class WeightTransferOptions
{
    public WeightTransferAlgorithm Algorithm = WeightTransferAlgorithm.KNN;
    public int KNN_K = 5;
    public float HeatDiffusionTime = 0.1f;
    public int HeatIterations = 50;
    public bool UseGPU = true;
    public int VoxelResolution = 64;
    public float MaxDistance = float.MaxValue;
    public bool PreserveVolume = true;
    public bool Symmetrize = true;
}

public static class WeightTransferEngine
{
    public static void TransferWeights(MeshData source, MeshData target, WeightTransferOptions options, IProgress<float> progress = null)
    {
        switch (options.Algorithm)
        {
            case WeightTransferAlgorithm.KNN:
                TransferWeightsKnn(source, target, options.KNN_K, options.MaxDistance);
                break;
            case WeightTransferAlgorithm.HeatDiffusion:
                TransferWeightsHeatDiffusion(source, target, options.HeatDiffusionTime, options.HeatIterations);
                break;
            case WeightTransferAlgorithm.BoundedBiharmonic:
                TransferWeightsBBW(source, target);
                break;
            case WeightTransferAlgorithm.GeodesicVoxel:
                TransferWeightsGeodesicVoxel(source, target, options.VoxelResolution);
                break;
        }
        
        // Post-process: enforce Metin2 constraints
        target.EnforceMaxInfluences();
        target.NormalizeWeights();
        
        if (options.Symmetrize)
            SymmetrizeWeights(target);
    }
}
```

## GPU ComputeShader (if available):
- HLSL compute shader for voxelization
- Parallel weight diffusion
- Fallback to CPU if no GPU

## Verification:
- All 4 algorithms produce valid weights
- KNN: < 100ms for 50k vertices
- Heat Diffusion: < 500ms
- BBW: < 2s (CPU) / < 200ms (GPU)
- Geodesic Voxel: < 1s (GPU)
- Results pass Metin2 validation