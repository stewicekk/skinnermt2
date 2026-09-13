# Skill: Mesh Deformation Validation & Auto-Fix

## Description
Validate armor mesh deformation against body mesh, detect penetrations, stretch, and provide auto-fix suggestions.

## File: `C:\rigapp\RigApp\Core\Validation\DeformationValidator.cs`

## Checks:

### 1. Penetration Detection (Armor vs Body):
```csharp
public static class PenetrationDetector
{
    public struct PenetrationResult
    {
        public List<PenetrationPoint> Points = new();
        public float MaxDepth;
        public float TotalVolume;
    }
    
    public struct PenetrationPoint
    {
        public int ArmorVertexIndex;
        public int BodyTriangleIndex;
        public Vector3 ArmorPosition;
        public Vector3 BodyClosestPoint;
        public float Depth;
        public Vector3 Normal;
    }
    
    public static PenetrationResult Detect(MeshData armor, Matrix4x4 armorTransform, MeshData body, Matrix4x4 bodyTransform)
    {
        // Transform to world space
        var armorVerts = TransformVertices(armor.Vertices, armorTransform);
        var bodyVerts = TransformVertices(body.Vertices, bodyTransform);
        var bodyTris = GetTriangles(body, bodyTransform);
        
        // Build BVH for body triangles
        var bvh = new BVH(bodyTris);
        
        var result = new PenetrationResult();
        
        Parallel.For(0, armor.VertexCount, vi =>
        {
            var point = armorVerts[vi];
            var query = bvh.QueryClosest(point);
            
            if (query.Distance < 0) // Inside body
            {
                lock (result.Points)
                {
                    result.Points.Add(new PenetrationPoint
                    {
                        ArmorVertexIndex = vi,
                        BodyTriangleIndex = query.TriangleIndex,
                        ArmorPosition = point,
                        BodyClosestPoint = query.ClosestPoint,
                        Depth = -query.Distance,
                        Normal = query.Normal
                    });
                    
                    if (-query.Distance > result.MaxDepth)
                        result.MaxDepth = -query.Distance;
                }
            }
        });
        
        return result;
    }
}
```

### 2. Stretch/Compression Analysis:
```csharp
public static class StretchAnalyzer
{
    public struct StretchResult
    {
        public float[] StretchRatios;     // Per triangle
        public float MaxStretch;
        public float MinStretch;
        public float AvgStretch;
        public List<int> OverstretchedTriangles; // > 1.5x
        public List<int> CompressedTriangles;    // < 0.5x
    }
    
    public static StretchResult Analyze(MeshData restMesh, MeshData deformedMesh)
    {
        var result = new StretchResult { StretchRatios = new float[restMesh.FaceCount] };
        
        for (int fi = 0; fi < restMesh.FaceCount; fi++)
        {
            // Rest triangle
            var r0 = restMesh.GetVertex(restMesh.Indices[fi*3]);
            var r1 = restMesh.GetVertex(restMesh.Indices[fi*3+1]);
            var r2 = restMesh.GetVertex(restMesh.Indices[fi*3+2]);
            
            // Deformed triangle
            var d0 = deformedMesh.GetVertex(deformedMesh.Indices[fi*3]);
            var d1 = deformedMesh.GetVertex(deformedMesh.Indices[fi*3+1]);
            var d2 = deformedMesh.GetVertex(deformedMesh.Indices[fi*3+2]);
            
            // Edge lengths
            float restArea = TriangleArea(r0, r1, r2);
            float defArea = TriangleArea(d0, d1, d2);
            
            float stretch = defArea / restArea;
            result.StretchRatios[fi] = stretch;
            
            if (stretch > 1.5f) result.OverstretchedTriangles.Add(fi);
            if (stretch < 0.5f) result.CompressedTriangles.Add(fi);
        }
        
        result.MaxStretch = result.StretchRatios.Max();
        result.MinStretch = result.StretchRatios.Min();
        result.AvgStretch = result.StretchRatios.Average();
        
        return result;
    }
}
```

### 3. Bone Influence Quality:
```csharp
public static class BoneQualityAnalyzer
{
    public struct BoneQualityResult
    {
        public float[] BoneUtilization;    // % vertices influenced
        public float[] WeightEntropy;      // Distribution uniformity
        public List<int> UnusedBones;
        public List<int> DominantBones;    // > 80% vertices
    }
}
```

## Auto-Fix Suggestions:
```csharp
public static class DeformationAutoFixer
{
    public enum FixType
    {
        PushOutPenetration,      // Move penetrating vertices along normal
        SmoothWeights,           // Laplacian smooth in problem areas
        ReduceStretch,           // Adjust weights to reduce stretch
        AddSupportBones,         // Suggest additional bones
        RemapWeights             // Re-map to better bone targets
    }
    
    public static List<FixSuggestion> GenerateFixes(PenetrationResult penetration, StretchResult stretch, MeshData mesh)
    {
        var fixes = new List<FixSuggestion>();
        
        if (penetration.MaxDepth > 0.01f)
            fixes.Add(new FixSuggestion { Type = FixType.PushOutPenetration, Priority = 1, ... });
        
        if (stretch.MaxStretch > 2.0f)
            fixes.Add(new FixSuggestion { Type = FixType.ReduceStretch, Priority = 2, ... });
        
        return fixes;
    }
}
```

## UI Panel:
- Real-time validation on pose change
- 3D viewport overlay: penetration heatmap, stretch colors
- Fix suggestions list with "Apply" buttons
- Export validation report (JSON + images)

## Verification:
- Detects all known Metin2 armor clipping issues
- Auto-fix resolves > 80% of penetrations
- Performance: < 200ms per validation