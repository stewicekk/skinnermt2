-- helper_bone_suggester.ms - Suggest basic bone positions for armor
-- Generated: 2025-09-21

try(destroyDialog BoneSugg)catch()
rollout BoneSugg "Bone Suggester" width:520 height:240
(
    button b_suggest "Suggest bones (mesh bounds center)"
    on b_suggest pressed do
    (
        if selection.count == 0 then (messageBox "Select mesh"); return()
        local obj = selection[1]
        local bbmin = nodeGetBoundingBox obj obj.min
        local bbmax = nodeGetBoundingBox obj obj.max
        local center = (bbmin + bbmax) / 2
        local b = BoneSys.CreateBone center (center + [0,10,0]) [0,0,2]
        b.name = obj.name + "_suggest_bone"
        messageBox ("Bone suggested: " + b.name)
    )
)
createDialog BoneSugg
