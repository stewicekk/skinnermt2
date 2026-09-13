# Skill: Cleanup Root Folder

## Description
Remove all redundant/legacy directories from C:\rigapp root, keeping only essential dependencies for the production pipeline.

## Instructions

### Directories to DELETE (recursive):
```
C:\rigapp\core\
C:\rigapp\engine\
C:\rigapp\pipelines\
C:\rigapp\agents\
C:\rigapp\bridges\
C:\rigapp\metin2_core\
C:\rigapp\Metin2GrannyTool\
C:\rigapp\GrannyConverter\
C:\rigapp\bin\
C:\rigapp\auto_train\
C:\rigapp\dataset\
C:\rigapp\gpu\
C:\rigapp\knowledgebase\
C:\rigapp\logs\
C:\rigapp\rag\
C:\rigapp\tests\
C:\rigapp\test_output\
C:\rigapp\ui\
C:\rigapp\videos\
C:\rigapp\apps\
C:\rigapp\configs\
```

### Directories to KEEP:
```
C:\rigapp\RigApp\              # Main C# WPF application
C:\rigapp\models\              # 182 GR2 source models
C:\rigapp\models\skeletons\    # Fixed skeletons (SMD→FBX)
C:\rigapp\exports\             # Output rigged models
C:\rigapp\exports\fbx\         # NEW: FBX exports via Noesis
C:\rigapp\exports\rigged\      # GR2 exports
C:\rigapp\tools\noesis\        # Noesis v4474 with GR2 plugins
C:\rigapp\tools\granny2 export settings\  # Reference
C:\rigapp\.opencode\           # OpenCode config and skills
```

### Verification:
- Run `Get-ChildItem C:\rigapp -Directory` - should show only kept directories
- No errors when building `C:\rigapp\RigApp\RigApp.csproj`

## Tools
- bash (Remove-Item -Recurse -Force)
- glob (verify remaining structure)