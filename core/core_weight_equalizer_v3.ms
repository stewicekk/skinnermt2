/* Generated: 2025-09-21 */


/* core_weight_equalizer_v3.ms - Weight Equalizer v3 (IMPLEMENTED)
Per-vertex neighborhood smoothing with top-N preservation.
*/
try(destroyDialog WeightEq3)catch()
rollout WeightEq3 "Weight Equalizer v3" width:460 height:260
(
    spinner sp_iters "Iterations" range:[1,8,3] type:#integer
    spinner sp_topn "Top-N preserve" range:[1,8,4] type:#integer
    button b_apply "Apply"
    on b_apply pressed do
    (
        undo "Weight Equalizer" on
        (
            for obj in selection do for m in obj.modifiers where classof m==Skin do
            (
                local mesh = snapshotAsMesh obj; local nv = mesh.numverts
                for it=1 to sp_iters.value do
                (
                    for v=1 to nv do
                    (
                        local neigh = meshop.getVertNeighbors mesh v
                        local accum = dotNetObject "System.Collections.Hashtable"
                        for nb in neigh do
                        (
                            local wc = skinOps.GetVertexWeightCount m nb
                            for i=1 to wc do
                            (
                                local bid = skinOps.GetVertexWeightBoneID m nb i
                                local w = skinOps.GetVertexWeight m nb i
                                if accum.ContainsKey bid then accum[bid] = accum[bid] + w else accum.Add bid w
                            )
                        )
                        local arr = #()
                        for k in accum.Keys do append arr #(k, accum[k])
                        qsort arr (fn A B -> A[2] > B[2])
                        if arr.count>0 then
                        (
                            local keep = for i=1 to (min sp_topn.value arr.count) collect arr[i]
                            local s=0.0; for p in keep do s+=p[2]; if s==0 then s=1.0
                            for i=1 to keep.count do keep[i][2] = keep[i][2] / s
                            skinOps.ReplaceVertexWeights m v keep
                        )
                    )
                )
            )
        )
        messageBox "Weight equalizer done."
    )
)
createDialog WeightEq3
