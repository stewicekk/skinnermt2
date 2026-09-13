-- pose_transfer.ms - Older pose transfer utility
-- Generated: 2025-09-21

try(destroyDialog PoseTransferLegacy)catch()
rollout PoseTransferLegacy "Pose Transfer (legacy)" width:520 height:200
(
    pickbutton pb_src "Source root"
    pickbutton pb_dst "Target root"
    button b_transfer "Transfer pose transforms"
    on b_transfer pressed do
    (
        if pb_src.pickNode == undefined or pb_dst.pickNode == undefined then (messageBox "Pick both roots"; return())
        for s in pb_src.pickNode.children do
        (
            local t = getNodeByName s.name
            if t != undefined then t.transform = s.transform
        )
        messageBox "Pose transfer applied (legacy name-match)."
    )
)
createDialog PoseTransferLegacy
