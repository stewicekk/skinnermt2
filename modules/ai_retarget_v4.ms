/* Generated: 2025-09-21 */


/* ai_retarget_v4.ms - Retarget v4 (IMPROVED)
Name + geometry based mapping and iterative refine.
*/
try(destroyDialog RetargetV4)catch()
rollout RetargetV4 "Auto Retarget v4 (IMPROVED)" width:700 height:340
(
    pickbutton pb_src "Source skeleton root"
    pickbutton pb_dst "Target skeleton root"
    spinner sp_iters "Refine iterations" range:[1,10,3] type:#integer
    button b_auto "Auto map by name & geometry"
    listbox lb_map "" width:660 height:200 items:#()
    on b_auto pressed do
    (
        if pb_src.pickNode==undefined or pb_dst.pickNode==undefined then (messageBox "Pick both roots"); return()
        local sroot = pb_src.pickNode; local droot = pb_dst.pickNode
        local sbones = for n in sroot.children collect n.name
        local dbones = for n in droot.children collect n.name
        local map = #()
        for sb in sbones do
        (
            local found = ""
            for db in dbones where (toLower db) == (toLower sb) do found = db
            append map #(sb, found)
        )
        for it=1 to sp_iters.value do
        (
            for i=1 to map.count do
            (
                local sname = map[i][1]
                local scn = getNodeByName sname
                if scn == undefined then continue
                local bestd = 1e9; local bestn = ""
                for dbn in droot.children do
                (
                    local dn = dbn
                    local dp = dn.pos - scn.pos
                    local dist = dp.length
                    if dist < bestd then (bestd = dist; bestn = dn.name)
                )
                map[i] = #(sname, bestn)
            )
        )
        lb_map.items = for p in map collect (p[1] + " -> " + p[2])
        messageBox "Auto-mapping finished."
    )
)
createDialog RetargetV4
