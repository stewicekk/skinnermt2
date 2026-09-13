# Skill: Create NoesisIntegration.cs C# Wrapper

## Description
Create a comprehensive C# wrapper for Noesis CLI to replace SMD pipeline with FBX pipeline.

## File: C:\rigapp\RigApp\Core\NoesisIntegration.cs

## Requirements:

### Namespace & Usings
```csharp
namespace RigApp.Core
{
    using System;
    using System.Diagnostics;
    using System.IO;
    using System.Threading.Tasks;
```

### Class: NoesisIntegration
Static class with these methods:

#### 1. GetNoesisPath()
```csharp
public static string GetNoesisPath()
{
    string path = RigAppPaths.NoesisExePath;
    if (File.Exists(path)) return path;
    
    // Fallback search
    string[] candidates = {
        Path.Combine(RigAppPaths.ToolsDirectory, "noesis", "Noesis.exe"),
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Noesis", "Noesis.exe"),
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Noesis", "Noesis.exe"),
    };
    foreach (var c in candidates)
        if (File.Exists(c)) return c;
    
    return null;
}
```

#### 2. IsAvailable()
```csharp
public static bool IsAvailable() => !string.IsNullOrEmpty(GetNoesisPath());
```

#### 3. ConvertGr2ToFbx(string gr2Path, string fbxPath)
- Run: `Noesis.exe "input.gr2" -quickconvert "output.fbx"`
- Capture stdout/stderr
- Return bool success + error message

#### 4. ConvertGr2ToSmd(string gr2Path, string smdPath)
- For legacy compatibility
- Run: `Noesis.exe "input.gr2" -quickconvert "output.smd"`

#### 5. ConvertFbxToGr2(string fbxPath, string gr2Path)
- Run: `Noesis.exe "input.fbx" -quickconvert "output.gr2"`

#### 6. BatchConvert(string inputDir, string outputDir, string inputExt, string outputExt, bool recursive)
- Use Noesis batch mode or loop files

#### 7. RunNoesisCommand(string args, int timeoutMs = 120000)
- Private helper to run Noesis.exe with args
- Return (bool success, string output, string error)

### Error Handling:
- Check exit code
- Log stdout/stderr
- Timeout handling
- File existence verification before/after

## Verification:
- Compile with `dotnet build`
- Test with a real GR2 file from `C:\rigapp\models\`
- Output FBX should be loadable in Blender/Noesis