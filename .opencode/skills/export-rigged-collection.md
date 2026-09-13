# Export Rigged Collection

Export all 182 models with proper Metin2 skinning.

## Process
1. Open RigApp.exe
2. Import base model (SMD or OBJ)
3. Select source skeleton from NPZ dropdown
4. Set KNN k=5
5. Click "Transfer Weights"
6. Click "Export Rigged GR2"
7. Repeat for all models OR use Batch Pro

## Batch Export
```powershell
Set-Location C:\rigapp
python production_pipeline.py --source models --target exports\rigged --k 5 --normalize --enforce4
```

## Output
- Individual: `exports/rigged/{set}_{model}_rigged.gr2`
- Total: 182 files, ~239 MB
- Compatible with Metin2 client (gr2 section format variant A)
