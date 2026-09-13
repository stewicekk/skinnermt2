# Skill: Dual Quaternion Skinning (DQS) Implementation

## Description
Implement Dual Quaternion Skinning for superior deformation quality, with blend factor between DQS and Linear Blend Skinning (LBS).

## File: `C:\rigapp\RigApp\Core\Skinning\DualQuaternionSkinning.cs`

## Theory:
- LBS: `v' = Σ(w_i * M_i * v)` - causes candy-wrapper artifact
- DQS: `v' = Normalize(Σ(w_i * q_i)) * v` - preserves volume, no candy-wrapper
- Blend: `v' = Lerp(LBS(v), DQS(v), blendFactor)`

## Data Structures:
```csharp
public struct DualQuaternion
{
    public Quaternion Real;      // Rotation
    public Quaternion Dual;      // Translation
    
    public static DualQuaternion FromMatrix(Matrix4x4 m)
    {
        var dq = new DualQuaternion();
        dq.Real = Quaternion.CreateFromRotationMatrix(m);
        var t = m.Translation;
        dq.Dual = new Quaternion(t.X * 0.5f, t.Y * 0.5f, t.Z * 0.5f, 0) * dq.Real;
        return dq;
    }
    
    public DualQuaternion Normalized()
    {
        float len = MathF.Sqrt(Real.X*Real.X + Real.Y*Real.Y + Real.Z*Real.Z + Real.W*Real.W);
        return new DualQuaternion { Real = Real / len, Dual = Dual / len };
    }
    
    public static DualQuaternion Blend(DualQuaternion a, DualQuaternion b, float t)
    {
        // Slerp for real, lerp for dual
        return new DualQuaternion
        {
            Real = Quaternion.Slerp(a.Real, b.Real, t),
            Dual = Quaternion.Lerp(a.Dual, b.Dual, t)
        }.Normalized();
    }
    
    public Vector3 TransformPoint(Vector3 v)
    {
        // v' = q * v * q* + 2 * (d * q* - q * d*)
        var q = Real;
        var d = Dual;
        var qConj = new Quaternion(-q.X, -q.Y, -q.Z, q.W);
        var dConj = new Quaternion(-d.X, -d.Y, -d.Z, d.W);
        
        var vQuat = new Quaternion(v.X, v.Y, v.Z, 0);
        var rotated = q * vQuat * qConj;
        var translated = 2 * (d * qConj - q * dConj);
        
        return new Vector3(rotated.X + translated.X, rotated.Y + translated.Y, rotated.Z + translated.Z);
    }
}
```

## Skinning Implementation:
```csharp
public static class DualQuaternionSkinning
{
    public static void ComputeBoneDualQuaternions(MeshData mesh, Matrix4x4[] boneMatrices, DualQuaternion[] outDQs)
    {
        for (int i = 0; i < mesh.BoneCount; i++)
        {
            outDQs[i] = DualQuaternion.FromMatrix(boneMatrices[i]);
        }
    }
    
    public static void SkinVertexDQS(Vector3 vertex, int[] boneIndices, float[] weights, DualQuaternion[] boneDQs, float dqsBlendFactor, out Vector3 result)
    {
        // Blend dual quaternions
        DualQuaternion blended = new DualQuaternion();
        bool first = true;
        
        for (int i = 0; i < Metin2Format.MaxBonesPerVertex; i++)
        {
            int boneIdx = boneIndices[i];
            float weight = weights[i];
            if (boneIdx < 0 || weight <= 0) continue;
            
            if (first)
            {
                blended = boneDQs[boneIdx];
                first = false;
            }
            else
            {
                blended = DualQuaternion.Blend(blended, boneDQs[boneIdx], weight);
            }
        }
        
        if (first) { result = vertex; return; }
        
        blended = blended.Normalized();
        Vector3 dqsResult = blended.TransformPoint(vertex);
        
        // Also compute LBS for blending
        Vector3 lbsResult = Vector3.Zero;
        for (int i = 0; i < Metin2Format.MaxBonesPerVertex; i++)
        {
            int boneIdx = boneIndices[i];
            float weight = weights[i];
            if (boneIdx < 0 || weight <= 0) continue;
            
            var m = Matrix4x4.CreateFromQuaternion(boneDQs[boneIdx].Real);
            m.Translation = new Vector3(boneDQs[boneIdx].Dual.X * 2, boneDQs[boneIdx].Dual.Y * 2, boneDQs[boneIdx].Dual.Z * 2);
            lbsResult += weight * Vector3.Transform(vertex, m);
        }
        
        // Blend
        result = Vector3.Lerp(lbsResult, dqsResult, dqsBlendFactor);
    }
    
    public static void SkinMeshDQS(MeshData mesh, Matrix4x4[] boneMatrices, float dqsBlendFactor, Vector3[] outVertices)
    {
        var boneDQs = new DualQuaternion[mesh.BoneCount];
        ComputeBoneDualQuaternions(mesh, boneMatrices, boneDQs);
        
        Parallel.For(0, mesh.VertexCount, vi =>
        {
            int baseIdx = vi * Metin2Format.MaxBonesPerVertex;
            SkinVertexDQS(
                new Vector3(mesh.Vertices[vi*3], mesh.Vertices[vi*3+1], mesh.Vertices[vi*3+2]),
                mesh.BoneIndices.AsSpan(baseIdx, Metin2Format.MaxBonesPerVertex).ToArray(),
                mesh.Weights.AsSpan(baseIdx, Metin2Format.MaxBonesPerVertex).ToArray(),
                boneDQs, dqsBlendFactor, out outVertices[vi]
            );
        });
    }
}
```

## GR2 Export with DQS:
- Store blend factor in GR2 user data
- Export both you stricttitle control

```csharp
public static void ExportWithDQS(string outputPath, MeshData mesh, float dqsBlendFactor = 0 JSON /
0
ahրոպ%
pmodels	
        Limit5ضى
2—¡
recontrol.cpp քան
modelscond SLinter