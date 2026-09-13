# Skill: Documentation & OpenCode Skills Update

## Description
Update all documentation and create/update 12 OpenCode skills for the new pipeline.

## Files to Create/Update:

### 1. User Manual: `C:\rigapp\DOCS\USER_MANUAL.md`
```markdown
# RigApp v2.0 - Metin2 Armor Rigging Pipeline

## Quick Start
1. Launch RigApp.exe
2. Import source armor (GR2/FBX)
3. Import target armor (GR2/FBX)
4. Click "Auto Map Bones"
5. Select algorithm (KNN/Heat/BBW/Voxel)
6. Click "Transfer Weights"
7. Review in 3D viewport + Weight Painter
8. Export as GR2 (for Metin2) or FBX (for Blender/Unity)

## Interface Overview
- **Toolbar**: File, View, Tools, Help
- **Viewport3D**: 3D model with bone/weight visualization
- **Bone Tree**: Hierarchy with visibility/lock/weight badges
- **Weight Painter**: Brush tools for manual editing
- **Pipeline Graph**: Visual node-based workflow
- **Status Bar**: Progress, validation, FPS

## Weight Transfer Algorithms
| Algorithm | Best For | Speed | Quality |
|-----------|----------|-------|---------|
| KNN | Similar topology | ⚡⚡⚡ Fast | Good |
| Heat Diffusion | Different topology | ⚡⚡ Medium | Excellent |
| Bounded Biharmonic | Production quality | ⚡ Slow | Best |
| Geodesic Voxel | Complex shapes | ⚡⚡ GPU | Excellent |

## Metin2 Constraints
- Max 4 bones per vertex (hard limit)
- Weight sum = 1.0 per vertex
- Max 128 bones per model
- Bone indices 0-127
- ASCII bone names only

## Batch Processing
- Configure armor sets in Batch Dialog
- Select algorithm per set
- Run overnight on 182 models
- Review validation reports

## Troubleshooting
- Noesis not found → Check Tools/Noesis path in Settings
- GR2 won't load → Verify Granny2 plugin in Noesis/plugins/python
- Weights look wrong → Check bone mapping, try different algorithm
- Export fails → Run Metin2 Validator, apply auto-fixes
```

### 2. Developer Guide: `C:\rigapp\DOCS\DEVELOPER_GUIDE.md`
- Architecture overview
- Adding new weight transfer algorithms
- Extending pipeline nodes
- Custom viewport tools
- Noesis plugin development

### 3. Update 12 OpenCode Skills:
1. `build-rigapp.md` - Updated for new pipeline
2. `run-regression.md` - Full test suite
3. `add-transfer-algorithm.md` - New algorithm template
4. `add-export-format.md` - New exporter template
5. `analyze-models.md` - Model analysis tools
6. `weight-transfer-knn.md` - KNN details
7. `convert-gr2-to-smd.md` → `convert-gr2-to-fbx.md` - Noesis conversion
8. `train-ml-model.md` - ML training pipeline
9. `run-batch-pipeline.md` - Batch processing
10. `validate-models.md` - Metin2 validation
11. `export-rigged-collection.md` - Collection export
12. `metin2-armor-system.md` - Armor system reference

### 4. API Documentation:
- XML comments on all public classes/methods
- Generate with `dotnet build /p:GenerateDocumentationFile=true`
- Output to `C:\rigapp\DOCS\API\`

## Verification:
- All skills load without errors
- User manual renders correctly
- API docs generate without warnings
- Examples in skills are tested and working