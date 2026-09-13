# Skill: Armor-to-Armor Weight Transfer (Batch 182 Models)

## Description
Automated pipeline to transfer weights from rigged armor to new armor meshes across all 182 models.

## File: `C:\rigapp\RigApp\Core\ArmorTransferPipeline.cs`

## Pipeline Steps:

### 1. Source Armor Detection:
```csharp
public class ArmorSet
{
    public string Name;           // "classic_chinese_costume"
    public string CharacterType;  // "warrior", "assassin", "shaman", "sura"
    public string Gender;         // "m", "w"
    public string SourceGr2Path;  // Rigged armor GR2
    public string TargetGr2Path;  // New armor GR2 (unrigged)
    public MeshData SourceMesh;   // Loaded via Noesis
    public MeshData TargetMesh;   // Loaded via Noesis
}
```

### 2. Bone Mapping:
```csharp
public static Dictionary<int, int> ComputeBoneMapping(MeshData source, MeshData target)
{
    var mapping = new Dictionary<int, int>();
    
    // 1. Exact name match
    for (int si = 0; si < source.BoneNames.Length; si++)
    {
        for (int ti = 0; ti < target.BoneNames.Length; ti++)
        {
            if (source.BoneNames[si] == target.BoneNames[ti])
            {
                mapping[ti] = si;
                break;
            }
        }
    }
    
    // 2. Fuzzy match (Levenshtein distance)
    // 3. Spatial proximity (bone center distance)
    // 4. Hierarchy structure match
    
    return mapping;
}
```

### 3. Weight Transfer with Bone Mapping:
```csharp
public static void TransferWithMapping(MeshData source, MeshData target, Dictionary<int, int> boneMap, WeightTransferOptions options)
{
    // Create virtual source with target's bone count
    var adaptedSource = new MeshData
    {
        Vertices = source.Vertices,
        BoneNames = target.BoneNames,
        BoneParents = target.BoneParents,
        Weights = new float[source.VertexCount * target.BoneNames.Length],
        BoneIndices = new int[source.VertexCount * target.BoneNames.Length]
    };
    
    // Remap weights using boneMap
    for (int vi = 0; vi < source.VertexCount; vi++)
    {
        for (int b = 0; b < Metin2Format.MaxBonesPerVertex; b++)
        {
            int srcBone = source.BoneIndices[vi * Metin2Format.MaxBonesPerVertex + b];
            float weight = source.Weights[vi * Metin2Format.MaxBonesPerVertex + b];
            if (srcBone >= 0 && boneMap.TryGetValue(srcBone, out int tgtBone))
            {
                adaptedSource.BoneIndices[vi * target.BoneNames.Length + b] = tgtBone;
                adaptedSource.Weights[vi * target.BoneNames.Length + b] = weight;
            }
        }
    }
    
    // Now transfer
    WeightTransferEngine.TransferWeights(adaptedSource, target, options);
}
```

### 4. Batch Processor:
```csharp
public static async Task<BatchResult> ProcessAllArmorSets(string modelsRoot, WeightTransferOptions options, IProgress<BatchProgress> progress)
{
    var armorSets = DiscoverArmorSets(modelsRoot);
    var results = new List<ArmorResult>();
    
    foreach (var set in armorSets)
    {
        progress?.Report(new BatchProgress { Current = set.Name, Total = armorSets.Count, ... });
        
        // Load via Noesis
        set.SourceMesh = await LoadViaNoesis(set.SourceGr2Path);
        set.TargetMesh = await LoadViaNoesis(set.TargetGr2Path);
        
        // Bone mapping
        var boneMap = ComputeBoneMapping(set.SourceMesh, set.TargetMesh);
        
        // Transfer
        TransferWithMapping(set.SourceMesh, set.TargetMesh, boneMap, options);
        
        // Validate & Export
        var validation = ValidateMetin2Constraints(set.TargetMesh);
        if (validation.IsValid)
        {
            string fbxOut = Path.Combine(RigAppPaths.FbxExportsDirectory, set.Name + "_rigged.fbx");
            string gr2Out = Path.Combine(RigAppPaths.Gr2ExportsDirectory, set.Name + "_rigged.gr2");
            
            Gr2Exporter.ExportToFbx(fbxOut, set.TargetMesh);
            NoesisIntegration.ConvertFbxToGr2(fbxOut, gr2Out);
            
            results.Add(new ArmorResult { Success = true, ... });
        }
    }
    
    return new BatchResult { Results = results };
}
```

## Verification:
- All 182 models processed
- Success rate > 95%
- Output GR2 loads in Metin2 client
- Weight validation passes