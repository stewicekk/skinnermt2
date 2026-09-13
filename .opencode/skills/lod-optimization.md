# Skill: LOD Generation & Mesh Optimization

## Description
Generate Level-of-Detail meshes for armor, reduce bone count, and optimize for Metin2 engine performance.

## File: `C:\rigapp\RigApp\Core\Optimization\LodGenerator.cs`

## LOD Generation:

### 1. Quadric Edge Collapse (Mesh Simplification):
```csharp
public static class MeshSimplifier
{
    public static MeshData Simplify(MeshData mesh, float targetRatio = 0.5f, int maxBones = 128)
    {
        // Quadric Error Metrics (QEM)
        // 1. Compute quadrics for each vertex
        // 2. Priority queue of edge collapses
        // 3. Collapse edges until target vertex count
        // 4. Update bone weights for new vertices
        
        int targetVertices = (int)(mesh.VertexCount * targetRatio);
        return SimplifyQEM(mesh, targetVertices, maxBones);
    }
    
    private static MeshData SimplifyQEM(MeshData mesh, int targetVertices, int maxBones)
    {
        // Implementation of Garland-Heckbert QEM
        // With bone weight preservation
    }
}
```

### 2. Bone Reduction:
```csharp
public static class BoneReducer
{
    public static MeshData ReduceBones(MeshData mesh, int targetBoneCount)
    {
        if (mesh.BoneCount <= targetBoneCount) return mesh;
        
        // 1. Compute bone importance (vertex influence sum)
        var boneImportance = new float[mesh.BoneCount];
        for (int vi = 0; vi < mesh.VertexCount; vi++)
        {
            for (int b = 0; b < Metin2Format.MaxBonesPerVertex; b++)
            {
                int idx = vi * Metin2Format.MaxBonesPerVertex + b;
                int boneIdx = mesh.BoneIndices[idx];
                float weight = mesh.Weights[idx];
                if (boneIdx >= 0) boneImportance[boneIdx] += weight;
            }
        }
        
        // 2. Sort bones by importance
        var sortedBones = boneImportance
            .Select((imp, idx) => (idx, imp))
            .OrderByDescending(x => x.imp)
            .ToList();
        
        // 3. Keep top N bones, remap others to nearest kept bone
        var keepMap = new Dictionary<int, int>();
        for (int i = 0; i < targetBoneCount; i++)
            keepMap[sortedBones[i].idx] = i;
        
        // 4. Remap weights
        var newMesh = mesh.Clone();
        newMesh.BoneNames = sortedBones.Take(targetBoneCount).Select(x => mesh.BoneNames[x.idx]).ToArray();
        newMesh.BoneParents = RemapParents(mesh.BoneParents, keepMap);
        
        for (int vi = 0; vi < mesh.VertexCount; vi++)
        {
            // Remap and renormalize
        }
        
        return newMesh;
    }
}
```

### 3. LOD Chain Generation:
```csharp
public class LodChain
{
    public MeshData Lod0;  // Original (highest detail)
    public MeshData Lod1;  // ~50% vertices
    public MeshData Lod2;  // ~25% vertices
    public MeshData Lod3;  // ~12% vertices (impostor)
    
    public float[] LodDistances = { 0f, 10f, 30f, 100f }; // World units
    
    public static LodChain Generate(MeshData source, LodSettings settings)
    {
        var chain = new LodChain { Lod0 = source };
        chain.Lod1 = MeshSimplifier.Simplify(source, settings.Lod1Ratio, settings.MaxBones);
        chain.Lod2 = MeshSimplifier.Simplify(chain.Lod1, settings.Lod2Ratio, settings.MaxBones);
        chain.Lod3 = GenerateImpostor(source); // Billboard or very low poly
        return chain;
    }
}
```

### 4. Texture Atlas Baking:
```csharp
public static class TextureAtlasBaker
{
    public static (Texture2D atlas, Vector2[] uvRemap) BakeAtlas(MeshData[] lodMeshes, int atlasSize = 2048)
    {
        // 1. Collect all textures from materials
        // 2. Pack rectangles (MaxRects algorithm)
        // 3. Render each material to atlas
        // 4. Remap UV coordinates
        // 5. Return single material + atlas texture
    }
}
```

## Metin2 Export:
- Export each LOD as separate GR2
- Naming: `armor_lod0.gr2`, `armor_lod1.gr2`, etc.
- Client loads based on camera distance

## Settings:
```csharp
public class LodSettings
{
    public float Lod1Ratio = 0.5f;
    public float Lod2Ratio = 0.25f;
    public float Lod3Ratio = 0.1f;
    public int MaxBones = 128;
    public bool GenerateImpostor = true;
    public int AtlasSize = 2048;
    public bool PreserveUVSeams = true;
}
```

## Verification:
- LOD0 = original quality
- LOD1: 50% verts, < 5% visual difference at 10m
- LOD2: 25% verts, acceptable at 30m
- LOD3: Impostor, acceptable at 100m+
- All LODs pass Metin2 validation
- Bone count ≤ 128 for all LODs