/* Generated: 2025-09-21 */


/* ai_pose_aware_transfer.ms - Pose-Aware Transfer (IMPROVED)
Transfers weights from source to target with nearest-vertex fallback.
*/
try(destroyDialog PoseAwareV2)catch()
rollout PoseAwareV2 "Pose-Aware Transfer v2" width:620 height:220
(
    pickbutton pb_src "Source mesh" 
    pickbutton pb_dst "Target mesh"
    spinner sp_top "Top-N apply" range:[1,8,4] type:#integer
    button b_transfer "Transfer weights (nearest-vertex fallback)"
    memo m_log "" width:600 height:120
    on b_transfer pressed do
    (
        if pb_src.pickNode==undefined or pb_dst.pickNode==undefined then (messageBox "Pick source and target"); return()
        local s = pb_src.pickNode; local d = pb_dst.pickNode
        local ms = snapshotAsMesh s; local md = snapshotAsMesh d
        m_log.text = ""
        local sskin = undefined; for mo in s.modifiers where classof mo==Skin do sskin=mo
        local dskin = undefined; for mo in d.modifiers where classof mo==Skin do dskin=mo
        if sskin == undefined or dskin == undefined then (messageBox "Both meshes must have Skin modifier"); return()
        for vt=1 to md.numverts do
        (
            local pv = md.getVert vt
            local best = 1; local bd = (ms.getVert 1 - pv).length
            for i=2 to ms.numverts do
            (
                local dist = (ms.getVert i - pv).length
                if dist < bd then (bd = dist; best = i)
            )
            local wc = skinOps.GetVertexWeightCount sskin best
            local arr = #()
            for i=1 to wc do append arr #(skinOps.GetVertexWeightBoneID sskin best i, skinOps.GetVertexWeight sskin best i)
            qsort arr (fn A B -> A[2] > B[2])
            local top = for i=1 to (min sp_top.value arr.count) collect arr[i]
            local ssum = 0.0; for p in top do ssum += p[2]
            if ssum == 0 then append top #([1,1.0]) else for p in top do p[2] = p[2] / ssum
            skinOps.ReplaceVertexWeights dskin vt top
        )
        update d
        messageBox "Transfer complete."
    )
)
createDialog PoseAwareV2
