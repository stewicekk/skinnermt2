/* Generated: 2025-09-21 */


/* mesh_splitter_armor.ms - Armor Mesh Splitter (BASIC)
Detach faces per material ID to separate armor pieces.
*/
try(destroyDialog MeshSplitter)catch()
rollout MeshSplitter "Armor Mesh Splitter" width:420 height:160
(
    button b_split "Split by material groups"
    on b_split pressed do
    (
        if selection.count==0 then (messageBox "Select mesh"); return()
        local obj = selection[1]; convertToPoly obj
        local mats = if obj.material!=undefined and obj.material.numsubs!=undefined then obj.material.numsubs else 1
        for i=1 to mats do
        (
            local faces = polyOp.getFacesUsingMatID obj i
            if faces.count > 0 then polyOp.detachFaces obj faces newObject:true
        )
        messageBox "Split attempt finished."
    )
)
createDialog MeshSplitter
