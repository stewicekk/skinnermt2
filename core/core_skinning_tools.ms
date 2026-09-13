/* Generated: 2025-09-21 */


/* core_skinning_tools.ms - Core skin helpers (IMPLEMENTED)
Functions:
 - normalize weights on selected objects
 - clear all vertex weights
 - snapshot weights to JSON
*/
try(destroyDialog SkinCore)catch()
rollout SkinCore "Core Skin Tools" width:520 height:260
(
    button b_norm "Normalize Selected Skin Weights"
    button b_clear "Clear Vertex Weights (selected)"
    button b_snapshot "Save Skin Snapshot (json)"
    on b_norm pressed do
    (
        undo "Normalize Weights" on
        (
            for obj in selection where (obj.modifiers.count > 0) do
            for m in obj.modifiers where classof m == Skin do
            (
                local nv = skinOps.GetNumberVertices m
                for v=1 to nv do
                (
                    local wc = skinOps.GetVertexWeightCount m v
                    local s = 0.0
                    for i=1 to wc do s += skinOps.GetVertexWeight m v i
                    if s>0 then for i=1 to wc do skinOps.SetVertexWeight m v (skinOps.GetVertexWeightBoneID m v i) (skinOps.GetVertexWeight m v i / s)
                )
            )
        )
        messageBox "Normalize complete."
    )
    on b_clear pressed do
    (
        undo "Clear Weights" on
        (
            for obj in selection do for m in obj.modifiers where classof m==Skin do
                for v=1 to skinOps.GetNumberVertices m do skinOps.ClearVertexWeights m v
        )
        messageBox "Cleared weights on selected objects."
    )
    on b_snapshot pressed do
    (
        if selection.count == 0 then (messageBox "Select skinned object first.") else
        (
            local obj = selection[1]; local sk = undefined
            for m in obj.modifiers where classof m==Skin do sk = m
            if sk == undefined then (messageBox "No Skin modifier on selection.") else
            (
                local savePath = getSaveFileName caption:"Save skin snapshot" filename:(obj.name + "_skin_snapshot.json")
                if savePath == undefined then return()
                local f = createFile savePath
                format "{\n  \"object\": \"%\",\n  \"vertices\":[\n" obj.name to:f
                local nv = skinOps.GetNumberVertices sk
                for v=1 to nv do
                (
                    local wc = skinOps.GetVertexWeightCount sk v
                    format "    [" to:f
                    for i=1 to wc do
                    (
                        format "[%,%]" (skinOps.GetVertexWeightBoneID sk v i) (skinOps.GetVertexWeight sk v i) to:f
                        if i<wc then format "," to:f
                    )
                    format "]" to:f
                    if v<nv then format ",\n" to:f else format "\n" to:f
                )
                format "  ]\n}\n" to:f; close f
                messageBox ("Snapshot saved to: " + savePath)
            )
        )
    )
)
createDialog SkinCore
