# Run Batch Pipeline

Process all 182 models through the production pipeline.

## C# RigApp Method
1. Launch RigApp.exe
2. Click "Batch Pro..." button
3. Configure:
   - Source: C:\rigapp\models
   - Target: C:\rigapp\exports\rigged
   - Algorithm: KNN (k=5)
   - Export: GR2
4. Click "Process"

## Python Pipeline Method
```powershell
Set-Location C:\rigapp
python production_pipeline.py --source models --target exports\rigged
```

## Verification
```powershell
Get-ChildItem C:\rigapp\exports\rigged -Filter *.gr2 | Measure-Object
```
Expected: 182 rigged GR2 files (~239 MB total)
