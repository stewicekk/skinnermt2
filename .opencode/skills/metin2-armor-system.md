# Metin2 Armor System Knowledge Base

## Skeleton Structure
- Bone naming: Bip01 hierarchy (Bip01, Bip01 Spine, Bip01 L Thigh, etc.)
- Maximum bones per skeleton: 60 (classic_chinese_costume)
- Bone transformations: position + rotation per frame

## Weight Painting
- Max 4 bone influences per vertex (Metin2 constraint)
- Weights normalized to sum = 1.0
- Bones indexed 0 to (bone_count - 1)
- Default fallback: parent bone with weight 1.0

## Armor Sets (21 total, 182 models)
- 6 base classes: assassin_m, assassin_w, shaman_m, shaman_w, sura_m, sura_w, warrior_m, warrior_w, wolfman
- Plus variant: lycan_w 
- Each armor set has 8-9 models per class
- Some sets have _m2 / _w2 variants (identical to _m / _w)

## Export Format (GR2)
- Magic: 0xC06CDE29 (')\xdel\xc0')
- Version: 456
- Sections: field_0=2 (variant A, compatible)
- Granny2.dll converts variant A only

## Import/Export Pipeline
- GR2 → SMD: granny2.dll (cmd /c "granny2.dll file -a")
- SMD → C# MeshData: SmdParser.Parse()
- MeshData → GR2: Gr2Exporter.ExportRigged()
- SMD weights → Training: analyze_smd.py → NPZ
- NPZ → ML Model: trainer.py → .pt checkpoint
