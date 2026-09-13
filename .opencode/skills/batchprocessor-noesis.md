# Skill: Update BatchProcessor for Noesis Pipeline

## Description
Integrate Noesis-based conversion steps into BatchProcessor.cs

## File: C:\rigapp\RigApp\BatchProcessor.cs

## Changes:

### 1. Add using statements:
```csharp
using RigApp.Core;
using System.IO;
```

### 2. Add Noesis conversion step in process pipeline:
```csharp
private static async Task<bool> ConvertGr2ToFbxViaNoesis(string gr2Path, string fbxPath, IProgress<string> log)
{
    log?.Report($"[Noesis] Converting {Path.GetFileName(gr2Path)} → FBX...");
    bool success = NoesisIntegration.ConvertGr2ToFbx(gr2Path, fbxPath);
    if (success)
        log?.Report($"[Noesis] Success: {fbxPath}");
    else
        log?.Report($"[Noesis] FAILED: {gr2Path}");
    return success;
}
```

### 3. Update ProcessArmorSet() or main processing loop:
- Before weight transfer: ensure source/target are FBX (convert via Noesis if GR2)
- After weight transfer: export result as FBX, then convert to GR2 via Noesis

### 4. Add new BatchStep enum:
```csharp
public enum BatchStep
{
    ConvertGr2ToFbx,
    LoadSkeletons,
    TransferWeights,
    EnforceConstraints,
    ExportFbx,
    ConvertFbxToGr2,
    ValidateOutput
}
```

### 5. Make ExportSmd public → ExportFbx:
```csharp
public static void ExportFbx(string outputPath, MeshData mesh)
{
    Gr2Exporter.ExportToFbx(outputPath, mesh);
}
```

### 6. Update BatchConfiguration to track FBX paths:
- SourceFbxPath, TargetFbxPath, OutputFbxPath

## Verification:
- Batch process runs end-to-end with Noesis
- GR2 → FBX → Weights → FBX → GR2 works
- Progress logging shows Noesis steps