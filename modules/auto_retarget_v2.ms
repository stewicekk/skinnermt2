-- auto_retarget_v2.ms - Simple name-based retarget helper
-- Generated: 2025-09-21
try(destroyDialog AutoRet2)catch()
rollout AutoRet2 "Auto Retarget v2" width:520 height:180
(
    pickbutton pb_src "Source root"
    pickbutton pb_dst "Target root"
    button b_map "Map by name"
    listbox lb_map "" width:480 height:100
    on b_map pressed do
    (
        if pb_src.pickNode == undefined or pb_dst.pickNode == undefined then (messageBox "Pick both roots"; return())
        local sroot = pb_src.pickNode; local droot = pb_dst.pickNode
        local sbones = for n in sroot.children collect n.name
        local dbones = for n in droot.children collect n.name
        lb_map.items = for s in sbones collect (
            local found = for d in dbones where (toLower d) == (toLower s) collect d
            if found.count > 0 then s + " -> " + found[1] else s + " -> (none)"
        )
        messageBox "Mapping complete (name-based)."
    )
)
createDialog AutoRet2
