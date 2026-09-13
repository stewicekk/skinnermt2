# Train ML Model for Weight Prediction

Trains skeleton GNN and weight network on real Metin2 armor data.

## Prerequisites
- Python 3.14+ with PyTorch (CPU)
- 182 NPZ training samples at `C:\rigapp\training\smd_extracted\`
- Checkpoints at `C:\rigapp\training\checkpoints\`

## Training
```powershell
Set-Location C:\rigapp
python core\training\trainer.py --epochs 10 --batch-size 16
```

## Monitoring
- Skeleton GNN loss: target < 2000 (current: ~2748)
- Weight Network loss: target < 0.01 (current: ~0.016)

## Output
- `training/checkpoints/skeleton_gnn_epoch_{n}.pt`
- `training/checkpoints/weight_network_epoch_{n}.pt`
