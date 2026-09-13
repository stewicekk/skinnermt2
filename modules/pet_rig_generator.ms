-- pet_rig_generator.ms - Quick pet rig (quadruped) generator
-- Generated: 2025-09-21

try(destroyDialog PetRig)catch()
rollout PetRig "Pet Rig Generator" width:420 height:200
(
    button b_gen "Generate simple pet rig"
    on b_gen pressed do
    (
        local root = BoneSys.CreateBone [0,0,0] [0,10,0] [0,0,2]
        root.name = "pet_root"
        local spine = BoneSys.CreateBone [0,10,0] [0,20,0] [0,0,2]
        spine.name = "pet_spine"
        spine.parent = root
        local tail = BoneSys.CreateBone [0,20,0] [0,30,0] [0,0,1]
        tail.name = "pet_tail"
        tail.parent = spine
        messageBox "Pet rig generated."
    )
)
createDialog PetRig
