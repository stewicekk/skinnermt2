-- lod_to_skin_sync.ms - Copy skin weights from base mesh to LODs using nearest vertex
-- Generated: 2025-09-21

try(destroyDialog LODSync)catch()
rollout LODSync "LOD to Skin Sync" width:560 height:220
(
    pickbutton pb_base "Base mesh"
    pickbutton pb_lod "LOD mesh"
    spinner sp_top "Top-N" range:[1,8,4] type:#integer
    button b_sync "Sync weights"
    on b_sync pressed do
    (
        if pb_base.pickNode == undefined or pb_lod.pickNode == undefined then (messageBox "Pick base and LOD meshes"; return())
        local b = pb_base.pickNode; local l = pb_lod.pickNode
        local ms = snapshotAsMesh b; local md = snapshotAsMesh l
        local bskin = undefined; for mo in b.modifiers where classof mo == Skin do bskin = mo
        local lskin = undefined; for mo in l.modifiers where classof mo == Skin do lskin = mo
        if bskin == undefined or lskin == undefined then (messageBox "Both must have Skin modifier"; return())
        for v=1 to md.numverts do
        (
            local pv = md.getVert v
            local best = 1; local bd = (ms.getVert 1 - pv).length
            for i=2 to ms.numverts do (
                local dist = (ms.getVert i - pv).length
                if dist < bd then (bd = dist; best = i)
            )
            local wc = skinOps.GetVertexWeightCount bskin best
            local arr = #()
            for i=1 to wc do append arr #(skinOps.GetVertexWeightBoneID bskin best i, skinOps.GetVertexWeight bskin best i)
            qsort arr (fn A B -> A[2] > B[2])
            local top = for i=1 to (min sp_top.value arr.count) collect arr[i]
            local s=0.0; for p in top do s+=p[2]
            if s==0 then s=1.0
            for i=1 to top.count do top[i][2] = top[i][2]/s
            skinOps.ReplaceVertexWeights lskin v top
        )
        update l
        messageBox "Sync complete."
    )
)
createDialog LODSync
