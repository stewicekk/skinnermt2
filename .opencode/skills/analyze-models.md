# Analyze 182 Models

Run comprehensive model statistics:
```powershell
$models = Get-ChildItem C:\rigapp\models -Recurse -Filter *.gr2
$total = $models.Count
Write-Host "Total models: $total"

# Size statistics
$sizes = $models | ForEach-Object { $_.Length }
$avg = ($sizes | Measure-Object -Average).Average
$min = ($sizes | Measure-Object -Minimum).Minimum
$max = ($sizes | Measure-Object -Maximum).Maximum
Write-Host "Size range: $min - $max bytes (avg: $avg)"
```

Check for orphaned files:
```powershell
$gr2 = Get-ChildItem C:\rigapp\models -Recurse -Filter *.gr2 | ForEach-Object { $_.BaseName }
$smd = Get-ChildItem C:\rigapp\models -Recurse -Filter *.smd | ForEach-Object { 
    $b = $_.BaseName; if ($b.EndsWith('.gr2')) { $b.Substring(0, $b.Length-4) } else { $b }
}
$gr2 | Where-Object { $_ -notin $smd } | ForEach-Object { Write-Host "Missing SMD: $_" }
$smd | Where-Object { $_ -notin $gr2 } | ForEach-Object { Write-Host "Missing GR2: $_" }
```
