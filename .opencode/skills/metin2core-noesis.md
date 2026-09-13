# Skill: Update Metin2Core for Noesis FBX Pipeline

## Description
Replace SMD-based loading with Noesis GR2→FBX conversion in Metin2Core.cs

## File: C:\rigapp\RigApp\Core\Metin2Core.cs

## Changes:

### 1. REMOVE SmdParser class entirely (lines 165-321)
### 2. REMOVE ObjParser class entirely (lines 574-662)

### 3. ADD FBX Parser using AssimpNet (new dependency) or Noesis
```csharp
// Add to RigApp.csproj: <PackageReference Include="AssimpNet" Version="4.1.0" />
// Then create FbxParser static class
public static class FbxParser
{
    public static MeshData Parse(string fbxPath)
    {
        var context = new Assimp.AssimpContext();
        var scene = context.ImportFile(fbxPath, 
            Assimp.PostProcessSteps.Triangulate | 
            Assimp.PostProcessSteps.GenerateSmoothNormals |
            Assimp.PostProcessSteps.LimitBoneWeights |
            Assimp.PostProcessSteps.ValidateDataStructure);
        
        if (scene == null || scene.MeshCount == 0)
            throw new InvalidOperationException($"Failed to load FBX: {fbxPath}");
        
        var mesh = new MeshData();
        // Extract first mesh (or combine all)
        var assimpMesh = scene.Meshes[0];
        
        // Vertices
        mesh.Vertices = new float[assimpMesh.VertexCount * 3];
        for (int i = 0; i < assimpMesh.VertexCount; i++)
        {
            var v = assimpMesh.Vertices[i];
            mesh.Vertices[i * 3] = v.X;
            mesh.Vertices[i * 3 + 1] = v.Y;
            mesh.Vertices[i * 3 + 2] = v.Z;
        }
        
        // Normals
        if (assimpMesh.HasNormals)
        {
            mesh.Normals = new float[assimpMesh.VertexCount * 3];
            for (int i = 0; i < assimpMesh.VertexCount; i++)
            {
                var n = assimpMesh.Normals[i];
                mesh.Normals[i * 3] = n.X;
                mesh.Normals[i * 3 + 1] = n.Y;
                mesh.Normals[i * 3 + 2] = n.Z;
            }
        }
        
        // TexCoords
        if (assimpMesh.HasTextureCoords(0))
        {
            mesh.TexCoords = new float[assimpMesh.VertexCount * 2];
            for (int i = 0; i < assimpMesh.VertexCount; i++)
            {
                var tc = assimpMesh.TextureCoordinateChannels[0][i];
                mesh.TexCoords[i * 2] = tc.X;
                mesh.TexCoords[i * 2 + 1] = tc.Y;
            }
        }
        
        // Indices
        var indices = new List<int>();
        foreach (var face in assimpMesh.Faces)
        {
            if (face.IndexCount == 3)
            {
                indices.AddRange(face.Indices);
            }
        }
        mesh.Indices = indices.ToArray();
        
        // Bones & Weights
        if (assimpMesh.HasBones)
        {
            int boneCount = scene.Meshes.Sum(m => m.BoneCount);
            // Map bone names to indices
            var boneNameToIndex = new Dictionary<string, int>();
            int boneIdx = 0;
            foreach (var m in scene.Meshes)
            {
                foreach (var b in m.Bones)
                {
                    if (!boneNameToIndex.ContainsKey(b.Name))
                        boneNameToIndex[b.Name] = boneIdx++;
                }
            }
            
            mesh.BoneNames = new string[boneNameToIndex.Count];
            foreach (var kv in boneNameToIndex.OrderBy(k => k.Value))
                mesh.BoneNames[kv.Value] = kv.Key;
            
            mesh.BoneParents = new int[mesh.BoneNames.Length];
            Array.Fill(mesh.BoneParents, -1);
            // Build parent hierarchy from scene nodes
            BuildBoneParents(scene.RootNode, boneNameToIndex, mesh.BoneParents);
            
            // Vertex weights
            mesh.Weights = new float[assimpMesh.VertexCount * Metin2Format.MaxBonesPerVertex];
            mesh.BoneIndices = new int[assimpMesh.VertexCount * Metin2Format.MaxBonesPerVertex];
            Array.Fill(mesh.BoneIndices, -1);
            
            for (int i = 0; i < assimpMesh.VertexCount; i++)
            {
                var weights = new List<(int bone, float weight)>();
                foreach (var bone in assimpMesh.Bones)
                {
                    foreach (var vw in bone.VertexWeights)
                    {
                        if (vw.VertexID == i)
                        {
                            int bIdx = boneNameToIndex[bone.Name];
                            weights.Add((bIdx, vw.Weight));
                        }
                    }
                }
                weights.Sort((a, b) => b.weight.CompareTo(a.weight));
                float sum = 0;
                for (int j = 0; j < Math.Min(Metin2Format.MaxBonesPerVertex, weights.Count); j++)
                {
                    mesh.BoneIndices[i * Metin2Format.MaxBonesPerVertex + j] = weights[j].bone;
                    mesh.Weights[i * Metin2Format.MaxBonesPerVertex + j] = weights[j].weight;
                    sum += weights[j].weight;
                }
                if (sum > 0)
                {
                    float inv = 1.0f / sum;
                    for (int j = 0; j < Metin2Format.MaxBonesPerVertex; j++)
                        mesh.Weights[i * Metin2Format.MaxBonesPerVertex + j] *= inv;
                }
            }
        }
        
        return mesh;
    }
    
    private static void BuildBoneParents(Assimp.Node node, Dictionary<string, int> boneMap, int[] boneParents)
    {
        if (boneMap.ContainsKey(node.Name))
        {
            int myIdx = boneMap[node.Name];
            if (node.Parent != null && boneMap.ContainsKey(node.Parent.Name))
            {
                boneParents[myIdx] = boneMap[node.Parent.Name];
            }
        }
        foreach (var child in node.Children)
            BuildBoneParents(child, boneMap, boneParents);
    }
}
```

### 4. UPDATE LoadFromNpz() to use Noesis:
```csharp
public static MeshData LoadFromNpz(string path)
{
    // Try Noesis GR2→FBX conversion first
    string gr2Path = path.Replace(".gr2.npz", ".gr2").Replace(".npz", ".gr2");
    if (!File.Exists(gr2Path))
    {
        // Reconstruct from NPZ naming
        string nameNoExt = Path.GetFileNameWithoutExtension(path);
        string[] parts = nameNoExt.Split('_');
        for (int i = 1; i < parts.Length; i++)
        {
            string armorSet = string.Join("_", parts.Take(i));
            string baseName = string.Join("_", parts.Skip(i));
            if (baseName.EndsWith(".gr2")) baseName = baseName[..^4];
            string candidate = RigAppPaths.FindGr2ForArmor(armorSet, baseName);
            if (candidate != null) { gr2Path = candidate; break; }
        }
    }
    
    if (File.Exists(gr2Path))
    {
        // Convert GR2 to FBX via Noesis
        string fbxPath = Path.ChangeExtension(gr2Path, ".fbx");
        if (!File.Exists(fbxPath))
        {
            if (!NoesisIntegration.ConvertGr2ToFbx(gr2Path, fbxPath))
                throw new InvalidOperationException($"Noesis conversion failed: {gr2Path}");
        }
        return FbxParser.Parse(fbxPath);
    }
    
    // Fallback to default skeleton
    return LoadDefaultSkeleton();
}
```

### 5. UPDATE Gr2Exporter - add ExportToFbx():
```csharp
public static void ExportToFbx(string outputPath, MeshData mesh)
{
    // Use Noesis to convert our GR2 to FBX, or write FBX directly via AssimpNet
    string tempGr2 = Path.ChangeExtension(outputPath, ".gr2");
    ExportRigged(tempGr2, mesh);
    NoesisIntegration.ConvertGr2ToFbx(tempGr2, outputPath);
    File.Delete(tempGr2);
}
```

## Verification:
- Add AssimpNet NuGet package
- Build succeeds
- LoadFromNpz works with real NPZ files
- ExportToFbx produces valid FBX