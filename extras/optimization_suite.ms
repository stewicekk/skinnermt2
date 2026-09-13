/* Generated: 2025-09-21 */


/* optimization_suite.ms - Optimization Suite (IMPLEMENTED)
LOD generator (ProOptimizer), weight pruning and weld helpers.
*/
try(destroyDialog OptSuite)catch()
rollout OptSuite "Optimization Suite" width:600 height:380
(
    spinner sp_lods "LOD levels" range:[1,6,3] type:#integer
    spinner sp_ratio "Reduce % per LOD" range:[5,90,40] type:#integer
    button b_gen "Generate LODs (using ProOptimizer if available)"
    button b_prune "Prune small weights (<0.01)"
    button b_weld "Weld close verts (tol 0.001)"
    memo m_log "" width:580 height:220
    on b_gen pressed do
    (
        if selection.count==0 then (messageBox "Select base mesh"); return()
        local base = selection[1]
        m_log.text = "Generating LODs...\n"
        for lvl=1 to sp_lods.value do
        (
            local copy = copy base name:(base.name + "_LOD" + (lvl as string))
            try
                (
                    local po = ProOptimizer()
                    addModifier copy po
                    po.VertexPercent = max 1 (100 - (lvl * sp_ratio.value))
                    po.calculate = true
                    m_log.text += ("LOD created: " + copy.name + "\n")
                )
            catch ( m_log.text += ("ProOptimizer failed for " + copy.name + "\n") )
        )
        messageBox "LOD generation attempted. See log."
    )
    on b_prune pressed do
    (
        local thr = 0.01
        for o in selection do for m in o.modifiers where classof m==Skin do
        (
            local nv = skinOps.GetNumberVertices m
            for v=1 to nv do
            (
                local arr = #()
                local wc = skinOps.GetVertexWeightCount m v
                for i=1 to wc do
                (
                    local bid = skinOps.GetVertexWeightBoneID m v i
                    local w = skinOps.GetVertexWeight m v i
                    if w >= thr then append arr #(bid,w)
                )
                if arr.count==0 then append arr #([1,1.0])
                local s=0.0; for p in arr do s+=p[2]; for i=1 to arr.count do arr[i][2] = arr[i][2]/s
                skinOps.ReplaceVertexWeights m v arr
            )
        )
        messageBox "Weight pruning complete."
    )
    on b_weld pressed do
    (
        for o in selection do ( convertToMesh o; meshop.weldVertsByThreshold o 0.001 )
        messageBox "Weld close verts attempted."
    )
)
createDialog OptSuite
