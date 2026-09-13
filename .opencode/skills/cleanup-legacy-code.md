# Skill: Cleanup Legacy SMD Code from C# Project

## Description
Remove all SMD-related code from the C# RigApp project, replacing with Noesis FBX pipeline references.

## Files to Modify:

### 1. C:\rigapp\RigApp\Core\Metin2Core.cs
- **REMOVE**: `SmdParser` class entirely (lines 165-321)
- **REMOVE**: `ObjParser` class (lines 574-662) - not needed with FBX
- **MODIFY**: `Metin2Core.LoadFromNpz()` - replace SMD lookup with Noesis GR2→FBX
- **MODIFY**: `Metin2Core.LoadDefaultSkeleton()` - load from FBX skeleton
- **ADD**: `using RigApp.Core;` for NoesisIntegration

### 2. C:\rigapp\RigApp\Core\NoesisIntegration.cs (NEW FILE)
- Create comprehensive Noesis CLI wrapper

### 3. C:\rigapp\RigApp\BatchProcessor.cs
- **REMOVE**: SMD export/import methods
- **ADD**: Noesis-based conversion steps

### 4. C:\rigapp\RigApp\BatchConfiguration.cs
- **MODIFY**: `ExportFormat` enum - ensure FBX is primary, remove SMD or mark legacy

### 5. C:\rigapp\RigApp\MainWindow.xaml.cs
- **REMOVE**: SMD-related button handlers (BtnExportSmd_Click)
- **ADD**: FBX export handlers

## Verification:
- `dotnet build -c Release` → 0 errors
- No references to `SmdParser`, `.smd` extension, `LoadFromNpz` SMD logic

## Tools
- read (analyze current code)
- edit (precise string replacements)
- write (new NoesisIntegration.cs)
- bash (dotnet build verification)