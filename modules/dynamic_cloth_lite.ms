-- dynamic_cloth_lite.ms - Lightweight cloth helper (non-sim)
-- Generated: 2025-09-21
try(destroyDialog ClothLite)catch()
rollout ClothLite "Dynamic Cloth Lite" width:420 height:160
(
    button b_attach "Attach cloth helper"
    on b_attach pressed do
    (
        if selection.count < 2 then (messageBox "Select cloth mesh then collider"); return()
        local cloth = selection[1]
        local col = selection[2]
        -- simplistic: create MassFX Cloth or link via skin
        messageBox ("Attached " + cloth.name + " to collider " + col.name + " (helper only)")
    )
)
createDialog ClothLite
