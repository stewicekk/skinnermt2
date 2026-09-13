# Skill: Update RigAppPaths for Noesis/FBX Pipeline

## Description
Update RigAppPaths static class to include Noesis and FBX paths, and skeleton lookup by character type.

## File: C:\rigapp\RigApp\Core\Metin2Core.cs (lines 718-728)

## New RigAppPaths Class:

```csharp
public static class RigAppPaths
{
    public static string RootDirectory { get; set; } = @"C:\rigapp";

    public static string ModelsDirectory => Path.Combine(RootDirectory, "models");
    public static string ExportsDirectory => Path.Combine(RootDirectory, "exports");
    public static string TrainingDirectory => Path.Combine(RootDirectory, "training");
    public static string SmdExtractedDirectory => Path.Combine(TrainingDirectory, "smd_extracted");

    // SKELETONS - now FBX-based
    public static string SkeletonsDirectory => Path.Combine(ModelsDirectory, "skeletons");
    public static string DefaultSkeletonPath => Path.Combine(SkeletonsDirectory, "default_m.fbx");
    
    // NOESIS INTEGRATION
    public static string ToolsDirectory => Path.Combine(RootDirectory, "tools");
    public static string NoesisDirectory => Path.Combine(ToolsDirectory, "noesis");
    public static string NoesisExePath => Path.Combine(NoesisDirectory, "Noesis.exe");
    public static string NoesisPluginsDirectory => Path.Combine(NoesisDirectory, "plugins", "python");
    
    // FBX OUTPUT
    public static string FbxExportsDirectory => Path.Combine(ExportsDirectory, "fbx");
    public static string Gr2ExportsDirectory => Path.Combine(ExportsDirectory, "rigged");

    // CHARACTER-SPECIFIC SKELETONS
    public static string GetSkeletonPathForCharacter(string characterType, string gender)
    {
        // characterType: "warrior", "assassin", "shaman", "sura", "default"
        // gender: "m", "w"
        string fileName = $"{characterType}_{gender}.fbx";
        string customPath = Path.Combine(SkeletonsDirectory, fileName);
        if (File.Exists(customPath))
            return customPath;
        
        // Fallback to default
        string defaultPath = Path.Combine(SkeletonsDirectory, $"default_{gender}.fbx");
        if (File.Exists(defaultPath))
            return defaultPath;
            
        return DefaultSkeletonPath;
    }

    // GR2 SOURCE LOOKUP
    public static string FindGr2ForArmor(string armorSet, string baseName)
    {
        // Look in models/{armorSet}/{baseName}.gr2
        string path = Path.Combine(ModelsDirectory, armorSet, baseName + ".gr2");
        if (File.Exists(path)) return path;
        
        // Recursive search
        var files = Directory.GetFiles(ModelsDirectory, baseName + ".gr2", SearchOption.AllDirectories);
        return files.Length > 0 ? files[0] : null;
    }
}
```

## Also Update Metin2Core.LoadDefaultSkeleton():
```csharp
public static MeshData LoadDefaultSkeleton(string characterType = "default", string gender = "m")
{
    string skelPath = RigAppPaths.GetSkeletonPathForCharacter(characterType, gender);
    if (File.Exists(skelPath))
        return FbxParser.Parse(skelPath); // NEW: FBX parser
    
    // Legacy fallback
    string defaultSmd = Path.Combine(ModelsDirectory, "classic_chinese_costume", "assassin_m.gr2.smd");
    if (File.Exists(defaultSmd))
        return SmdParser.Parse(defaultSmd);
    
    return new MeshData { BoneNames = new[] { "Bip01", "Bip01 Pelvis", "Bip01 Spine" }, BoneParents = new[] { -1, 0, 1 } };
}
```

## Verification:
- Build succeeds
- Paths resolve correctly
- Skeleton lookup works for all character types