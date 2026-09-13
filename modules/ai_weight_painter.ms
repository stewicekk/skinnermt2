-- ai_weight_painter.ms - Simple weight painter (non-brush) for Skin modifier
-- Generated: 2025-09-21

try(destroyDialog WeightPainter)catch()
rollout WeightPainter "Weight Painter (Simple)" width:420 height:220
(
    pickbutton pb_bone "Pick bone (select in scene)" autoDisplay:true
    spinner sp_weight "Weight" range:[0.0,1.0,0.5] type:#float
    button b_apply "Apply to selected verts"
    on b_apply pressed do
    (
        if pb_bone.pickNode == undefined then (messageBox "Pick a bone first"; return())
        local bone = pb_bone.pickNode
        for obj in selection where obj.modifiers.count > 0 do
            for m in obj.modifiers where classof m == Skin do
            (
                local bIndex = (for i=1 to skinOps.GetNumberBones m where (skinOps.GetBoneName m i) == bone.name collect i)[1]
                if bIndex == undefined then messageBox ("Bone not found in Skin: " + bone.name) else
                (
                    local verts = polyOp.getVertSelection obj
                    if verts == undefined or verts.count == 0 then messageBox "Select vertices in sub-object mode" else
                    (
                        for v in verts do skinOps.SetVertexWeight m v bIndex sp_weight.value
                        update obj
                        messageBox "Applied weights."
                    )
                )
            )
    )
)
createDialog WeightPainter
