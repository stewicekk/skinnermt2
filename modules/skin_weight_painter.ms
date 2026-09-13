-- skin_weight_painter.ms - Another painter UI with simple falloff
-- Generated: 2025-09-21

try(destroyDialog SWPainter)catch()
rollout SWPainter "Skin Weight Painter" width:420 height:260
(
    pickbutton pb_bone "Pick bone" autoDisplay:true
    spinner sp_weight "Weight" range:[0.0,1.0,0.5] type:#float
    spinner sp_radius "Radius" range:[0.1,10.0,1.0] type:#float
    button b_paint "Paint (apply to verts within radius from selection center)"
    on b_paint pressed do
    (
        if pb_bone.pickNode == undefined then (messageBox "Pick bone first"; return())
        if selection.count == 0 then (messageBox "Select mesh"); return()
        local mesh = selection[1]
        local center = mesh.pos
        for m in mesh.modifiers where classof m == Skin do
        (
            local bones = for i=1 to skinOps.GetNumberBones m collect skinOps.GetBoneName m i
            local targetIndex = (for i=1 to bones.count where bones[i] == pb_bone.pickNode.name collect i)[1]
            if targetIndex == undefined then (messageBox "Bone not in skin"; return())
            local sm = snapshotAsMesh mesh
            for v = 1 to sm.numverts do
            (
                local wpos = sm.getVert v * mesh.transform
                local d = (wpos - center).length
                if d <= sp_radius.value then skinOps.SetVertexWeight m v targetIndex sp_weight.value
            )
            update mesh
        )
        messageBox "Paint applied (simple radius)."
    )
)
createDialog SWPainter
