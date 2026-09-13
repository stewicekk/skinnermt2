-- multi_rig_preview.ms - Spawn quick rig previews
-- Generated: 2025-09-21

try(destroyDialog MultiRig)catch()
rollout MultiRig "Multi Rig Preview" width:520 height:220
(
    button b_spawn "Spawn 3 rig previews"
    on b_spawn pressed do
    (
        if selection.count == 0 then (messageBox "Select mesh to preview"); return()
        local base = selection[1]
        for i = 1 to 3 do (
            local c = copy base name:(base.name + "_preview" + (i as string))
            c.pos.x += i*30
        )
        messageBox "Spawned previews. Move them manually to compare." 
    )
)
createDialog MultiRig
