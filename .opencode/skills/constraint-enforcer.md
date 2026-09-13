# Skill: Metin2 Constraint Enforcer & Validator

## Description
Hard enforcement of Metin2 engine constraints with detailed validation reports.

## File: `C:\rigapp\RigApp\Core\Metin2Validator.cs`

## Constraints:

### 1. Bone Limits:
- Max 128 bones per model (0-127)
- Max 4 bone influences per vertex
- Bone indices must be valid (0 to BoneCount-1)

### 2. Weight Constraints:
- Weights per vertex sum to 1.0 (±0.001 tolerance)
- Each weight ≥ 0.0
- No NaN or Infinity values

### 3. Mesh Constraints:
- Triangle count < 65535 (16-bit index buffer)
- Vertex count < 65535 (per submesh)
- UV coordinates in [0,1] range (or valid tiling)
- Normals normalized (length = 1.0 ± 0.01)

### 4. Skeleton Constraints:
- Root bone index 0, parent = -1
- No cycles in bone hierarchy
- All bones reachable from root
- Bone names unique, ASCII only, < 64 chars

### 5. GR2 Format Constraints:
- Magic = 0xC06CDE29
- Version = 456 (Granny 2.11+)
- Proper alignment (4-byte boundaries)
- Bone count matches header

## Validator Class:
```csharp
public static class Metin2Validator
{
    public class ValidationResult
    {
        public bool IsValid;
        public List<ValidationError> Errors = new();
        public List<ValidationWarning> Warnings = new();
        public ValidationStats Stats = new();
    }
    
    public class ValidationError
    {
        public enum Severity { Error, Critical }
        public Severity Severity;
        public string Code;        // "WEIGHT_SUM", "BONE_INDEX_OOB", etc.
        public string Message;
        public int VertexIndex;    // -1 if N/A
        public int BoneIndex;      // -1 if N/A
        public object Context;     // Additional data
    }
    
    public static ValidationResult Validate(MeshData mesh, string gr2Path = null)
    {
        var result = new ValidationResult();
        
        // Weight validation
        for (int vi = 0; vi < mesh.VertexCount; vi++)
        {
            float sum = 0;
            int influenceCount = 0;
            for (int b = 0; b < Metin2Format.MaxBonesPerVertex; b++)
            {
                int idx = vi * Metin2Format.MaxBonesPerVertex + b;
                int boneIdx = mesh.BoneIndices[idx];
                float weight = mesh.Weights[idx];
                
                if (boneIdx >= 0)
                {
                    influenceCount++;
                    if (boneIdx >= mesh.BoneCount)
                        result.Errors.Add(new ValidationError { 
                            Severity = ValidationError.Severity.Critical,
                            Code = "BONE_INDEX_OOB", 
                            Message = $"Vertex {vi}: bone index {boneIdx} >= bone count {mesh.BoneCount}",
                            VertexIndex = vi, BoneIndex = boneIdx 
                        });
                    if (weight < 0 || float.IsNaN(weight) || float.IsInfinity(weight))
                        result.Errors.Add(new ValidationError { ... });
                    sum += weight;
                }
            }
            
            if (influenceCount > Metin2Format.MaxBonesPerVertex)
                result.Errors.Add(new ValidationError { 
                    Code = "TOO_MANY_INFLUENCES", 
                    Message = $"Vertex {vi}: {influenceCount} influences (max 4)", 
                    VertexIndex = vi 
                });
            
            if (influenceCount > 0 && Math.Abs(sum - 1.0f) > 0.001f)
                result.Warnings.Add(new ValidationWarning { 
                    Code = "WEIGHT_SUM", 
                    Message = $"Vertex {vi}: weights sum to {sum:F4} (expected 1.0)", 
                    VertexIndex = vi 
                });
        }
        
        // Skeleton validation
        ValidateSkeleton(mesh, result);
        
        // Mesh validation
        ValidateMesh(mesh, result);
        
        // GR2 file validation (if path provided)
        if (gr2Path != null && File.Exists(gr2Path))
            ValidateGr2File(gr2Path, result);
        
        result.IsValid = result.Errors.Count == 0;
        return result;
    }
}
```

## Auto-Fixer:
```csharp
public static class Metin2AutoFixer
{
    public static int FixWeights(MeshData mesh)
    {
        int fixedCount = 0;
        for (int vi = 0; vi < mesh.VertexCount; vi++)
        {
            // Clamp negative weights
            // Renormalize
            // Prune excess influences
            // Clamp bone indices
        }
        return fixedCount;
    }
}
```

## Integration:
- Run automatically after every weight transfer
- Run before every export
- Show results in UI (ErrorList panel)
- Export validation report as JSON

## Verification:
- Catches all known Metin2 export issues
- Auto-fixer resolves > 90% of issues
- Report is human-readable
- Performance: < 50ms for 50k vertices