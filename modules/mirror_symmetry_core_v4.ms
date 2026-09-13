
try(destroyDialog MirrorCoreV4)catch()
rollout MirrorCoreV4 "Mirror & Symmetry Core v4" width:820 height:540
(
    label lbl_info "Select source mesh. Build pairs, create mirrored copy, then apply weights."
    spinner sp_voxel "Voxel size" range:[0.0005,20.0,0.05] type:#float value:0.05
    spinner sp_tol "Y/Z tolerance (for candidate match)" range:[0.0,1.0,0.01] type:#float value:0.02
    checkbox cb_signedX "Use signed-X mirror (keep sign consistency)" checked:true
    button b_build "Build mirror pairs (spatial hash)"
    button b_create_geo "Create mirrored geometry (copy)"
    button b_mapbones "Auto-map bones (heuristic)"
    button b_apply_weights "Apply mirrored weights to mirrored copy"
    checkbox cb_dryrun "Dry-run (do not write weights)" checked:false
    button b_export_map "Export bone-map JSON"
    button b_import_map "Import bone-map JSON"
    memo m_log "" width:800 height:300

    local pairs = undefined
    local boneMap = undefined

    fn vecToKey v voxel = (
        local vx = floor (v.x / voxel)
        local vy = floor (v.y / voxel)
        local vz = floor (v.z / voxel)
        (vx as string) + "_" + (vy as string) + "_" + (vz as string)
    )

    fn buildSpatialHash meshObj voxel tol = (
        local sm = snapshotAsMesh meshObj
        local hash = dotNetObject "System.Collections.Hashtable"
        local n = sm.numverts
        for i = 1 to n do (
            local v = sm.getVert i
            local k = vecToKey v voxel
            if hash.ContainsKey k then hash.Item[k] = append hash.Item[k] i else hash.Add k, #(i)
        )
        local pairsLocal = #()
        for i = 1 to n do (
            local vi = sm.getVert i
            local target = -vi.x
            local key = vecToKey [target,vi.y,vi.z] voxel
            local found = undefined
            for ox = -1 to 1 do for oy = -1 to 1 do for oz = -1 to 1 do (
                local nk = ((floor(target/voxel)+ox) as string) + "_" + ((floor(vi.y/voxel)+oy) as string) + "_" + ((floor(vi.z/voxel)+oz) as string)
                if hash.ContainsKey nk then (
                    local bucket = hash.Item[nk]
                    local best = 0; local bd = 1e9
                    for idx in bucket do (
                        local vj = sm.getVert idx
                        local dy = abs(vj.y - vi.y); local dz = abs(vj.z - vi.z)
                        if dy <= tol and dz <= tol then (
                            local d = (dy*dy + dz*dz)
                            if d < bd then (bd = d; best = idx)
                        )
                    )
                    if best > 0 then (found = best; exit)
                )
            )
            if found != undefined then append pairsLocal #(i, found)
        )
        pairsLocal
    )

    on b_build pressed do (
        if selection.count == 0 then (messageBox "Select mesh first"; return())
        local obj = selection[1]
        undo "Build Mirror Pairs (v4)" on (
            pairs = buildSpatialHash obj sp_voxel.value sp_tol.value
        )
        m_log.text = ("Built pairs: " + (pairs.count as string) + "\n") + m_log.text
        messageBox ("Pairs built: " + (pairs.count as string))
    )

    on b_create_geo pressed do (
        if selection.count == 0 then (messageBox "Select source mesh"; return())
        local src = selection[1]
        undo "Create mirrored geometry" on (
            local dst = copy src name:(src.name + "_mirrored")
            convertToMesh dst
            local s = snapshotAsMesh dst
            for i = 1 to s.numverts do (
                local p = getVert s i
                setVert dst i ([-p.x, p.y, p.z])
            )
            update dst
            m_log.text = ("Created mirrored copy: " + dst.name + "\n") + m_log.text
        )
        messageBox "Mirrored geometry created."
    )

    on b_mapbones pressed do (
        if selection.count == 0 then (messageBox "Select skinned source mesh to map bones from"; return())
        local src = selection[1]
        local sk = undefined
        for m in src.modifiers where classof m == Skin do sk = m
        if sk == undefined then (messageBox "Source has no Skin modifier"; return())
        boneMap = dotNetObject "System.Collections.Hashtable"
        for i = 1 to skinOps.GetNumberBones sk do (
            local bname = skinOps.GetBoneName sk i
            local candidates = #(replaceString bname "L_" "R_", replaceString bname "R_" "L_", replaceString bname "_L" "_R", replaceString bname "_R" "_L", replaceString bname "left" "right", replaceString bname "right" "left")
            local found = undefined
            for cand in candidates do (
                for n in objects do (
                    if (toLower n.name) == (toLower cand) then (found = n.name; exit)
                )
                if found != undefined then exit
            )
            if found == undefined then found = bname
            boneMap.Add bname found
        )
        local out = getSaveFileName caption:"Save bone map JSON" filename:"bone_map.json"
        if out != undefined then (
            local fh = createFile out
            format "{\n" to:fh
            local first = true
            for k in boneMap.Keys do (
                if not first then format ",\n" to:fh
                format "  \"%\": \"%\" " k (boneMap.Item[k]) to:fh
                first = false
            )
            format "\n}\n" to:fh; close fh
            m_log.text = ("Exported bone map: " + out + "\n") + m_log.text
        )
        messageBox "Bone map heuristic created (and optionally exported)."
    )

    on b_import_map pressed do (
        local f = getOpenFileName caption:"Import bone map JSON"
        if f == undefined then return()
        try (
            local fh = openFile f; local txt = readLine fh; local full = txt; while not eof fh do full += readLine fh; close fh
            boneMap = dotNetObject "System.Collections.Hashtable"
            local lines = filterString full "\n"
            for L in lines do (
                local s = trimLeft L; s = trimRight s
                if findString s "\"":\""> 0 then (
                    local parts = filterString s "\"":\""
                    local src = trimRight (substring parts[1] 2 (parts[1].count-2))
                    local dst = trimRight (substring parts[2] 2 (parts[2].count-3))
                    boneMap.Add src dst
                )
            )
            m_log.text = ("Imported bone map from: " + f + "\n") + m_log.text
            messageBox "Bone map imported."
        ) catch (messageBox ("Failed to import: " + (getCurrentException()))) 
    )

    on b_export_map pressed do (
        if boneMap == undefined then (messageBox "No bone map to export"; return())
        local out = getSaveFileName caption:"Save bone map JSON" filename:"bone_map.json"
        if out == undefined then return()
        local fh = createFile out
        format "{\n" to:fh
        local first = true
        for k in boneMap.Keys do (
            if not first then format ",\n" to:fh
            format "  \"%\": \"%\" " k (boneMap.Item[k]) to:fh
            first = false
        )
        format "\n}\n" to:fh; close fh
        m_log.text = ("Exported bone map: " + out + "\n") + m_log.text
        messageBox "Bone map exported."
    )

    on b_apply_weights pressed do (
        if selection.count == 0 then (messageBox "Select source mesh (original)"; return())
        local src = selection[1]; local dst = getNodeByName (src.name + "_mirrored")
        if dst == undefined then (messageBox "Mirrored copy not found; create it first"; return())
        local sskin = undefined; local dskin = undefined
        for m in src.modifiers where classof m == Skin do sskin = m
        for m in dst.modifiers where classof m == Skin do dskin = m
        if sskin == undefined or dskin == undefined then (messageBox "Both meshes must have Skin modifier"; return())
        if pairs == undefined or pairs.count == 0 then (messageBox "Pairs not built; building now with voxel " + (sp_voxel.value as string); pairs = buildSpatialHash src sp_voxel.value sp_tol.value)
        local mapped = dotNetObject "System.Collections.Hashtable"
        if boneMap == undefined then (
            for i = 1 to skinOps.GetNumberBones sskin do (
                local sname = skinOps.GetBoneName sskin i
                local foundIndex = undefined
                for j = 1 to skinOps.GetNumberBones dskin do (
                    if (toLower (skinOps.GetBoneName dskin j)) == (toLower sname) then ( foundIndex = j; exit )
                )
                if foundIndex == undefined then foundIndex = 1
                mapped.Add i foundIndex
            )
        ) else (
            for i = 1 to skinOps.GetNumberBones sskin do (
                local sname = skinOps.GetBoneName sskin i
                local dstName = boneMap.Item[sname] if boneMap.ContainsKey sname else sname
                local foundIndex = 1
                for j = 1 to skinOps.GetNumberBones dskin do if (toLower (skinOps.GetBoneName dskin j)) == (toLower dstName) then ( foundIndex = j; exit )
                mapped.Add i foundIndex
            )
        )
        undo "Apply Mirrored Weights (v4)" on (
            for k = 1 to pairs.count do (
                local sidx = pairs[k][1]; local didx = pairs[k][2]
                local wc = skinOps.GetVertexWeightCount sskin sidx
                local arr = #()
                for wi = 1 to wc do (
                    local bid = skinOps.GetVertexWeightBoneID sskin sidx wi
                    local w = skinOps.GetVertexWeight sskin sidx wi
                    local mappedIdx = mapped.Item[bid]
                    append arr #(mappedIdx, w)
                )
                local ssum = 0.0; for p in arr do ssum += p[2]
                if ssum == 0 then append arr #([1,1.0]) else for i=1 to arr.count do arr[i][2] = arr[i][2] / ssum
                if not cb_dryrun.checked then skinOps.ReplaceVertexWeights dskin didx arr
            )
        )
        update dst
        m_log.text = ("Applied mirrored weights to: " + dst.name + "\n") + m_log.text
        messageBox "Mirrored weights applied (or dry-run simulated)."
    )
)
createDialog MirrorCoreV4


-- External pairing helpers: export vertices to JSON and import pairs JSON produced by external Python
button b_export_vertices "Export vertices JSON (for Python pairing)"
button b_import_pairs "Import pairs JSON (apply to mirror)"
on b_export_vertices pressed do (
    if selection.count == 0 then (messageBox "Select mesh first"; return())
    local obj = selection[1]
    local out = getSaveFileName caption:"Export vertices JSON" filename:(obj.name + "_verts.json")
    if out == undefined then return()
    try ( exportVertexPositions obj out ) catch ( messageBox "Export failed; ensure kd_tree_max.ms is loaded." )
)

on b_import_pairs pressed do (
    local f = getOpenFileName caption:"Open pairs JSON (from Python)"
    if f == undefined then return()
    try (
        local fh = openFile f
        local txt = readLine fh
        local full = txt
        while not eof fh do full += readLine fh
        close fh
        local logfn = getDir #userScripts + "\\imported_pairs_raw.txt"
        local fhw = createFile logfn
        format "%\n" full to:fhw
        close fhw
        messageBox ("Imported pairs file saved to: " + logfn + "\nPlease verify and convert to MaxScript array if needed.")
    ) catch (messageBox "Failed to import pairs file")
)

-- Note: For robust external pairing, run Python script (ai_backend/kd_tree_cli.py) on exported vertices JSON to produce pairs JSON with indices.
-- Then import pairs JSON using the UI above and apply weights manually via skinOps or use the mirrored apply workflow.
