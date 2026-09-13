/* Generated: 2025-09-21 */


/* core_rigging_v4.ms - Rigging Core v4 (IMPLEMENTED)
Provides quick rig creation, mirror and reparent helpers.
*/
try(destroyDialog RigCoreV4)catch()
rollout RigCoreV4 "Rigging Core v4" width:520 height:240
(
    button b_create_biped "Create simple biped rig"
    button b_mirror_sel "Mirror Selected Bone Scales X"
    button b_reparent "Reparent child->parent (select child then parent)"
    editText et_rootname "Root name" text:"root" width:300
    on b_create_biped pressed do
    (
        undo "Create Simple Biped" on
        (
            local root = et_rootname.text
            local b1 = BoneSys.CreateBone [0,0,0] [0,12,0] [0,0,2]
            local b2 = BoneSys.CreateBone [0,12,0] [0,24,0] [0,0,2]
            local b3 = BoneSys.CreateBone [0,24,0] [0,36,0] [0,0,2]
            b1.name = root + "_spine_01"; b2.name = root + "_spine_02"; b3.name = root + "_spine_03"
            messageBox ("Created bones: " + b1.name + ", " + b2.name + ", " + b3.name)
        )
    )
    on b_mirror_sel pressed do
    (
        undo "Mirror Bone ScaleX" on
        (
            for o in selection where classof o==Bone do
            (
                o.scale.x = -o.scale.x
            )
        )
        messageBox "Mirrored bone scale X."
    )
    on b_reparent pressed do
    (
        if selection.count < 2 then (messageBox "Select child then parent") else
        (
            local child = selection[1]; local parent = selection[2]
            child.parent = parent; messageBox (child.name + " parented to " + parent.name)
        )
    )
)
createDialog RigCoreV4
