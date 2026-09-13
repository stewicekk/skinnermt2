# Weight Transfer with KNN

Transfers bone weights from a source skeleton to a target mesh using K-Nearest Neighbors.

Algorithm:
1. For each vertex in target mesh, find K nearest vertices in source mesh
2. Accumulate bone weights from source vertices weighted by inverse distance
3. Enforce max 4 bones per vertex (Metin2 constraint)
4. Normalize weights to sum to 1.0

Parameters:
- K: number of neighbors (default: 5)
- Source skeleton: NPZ or SMD file with reference weights

Usage:
1. Open RigApp.exe
2. Import target mesh (OBJ or SMD)
3. Select source skeleton from dropdown
4. Set K value (1-20)
5. Click "Transfer Weights"
6. Export as GR2 or SMD
