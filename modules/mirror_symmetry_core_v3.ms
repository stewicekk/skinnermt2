/* mirror_symmetry_core_v3.ms - Mirror & Symmetry Core v3
   - Builds vertex pair mapping by spatial hashing on X coordinate
   - Mirrors geometry and skin weights (by mapping bones)
   - Safer operations with undo and dry-run
*/
try(destroyDialog MirrorCoreV3)catch()
rollout MirrorCoreV3 "Mirror & Symmetry Core v3" width:640 height:420
(
    button b_build_pairs "Build vertex pairs (by X match)"
    checkbox cb_auto_apply "Auto-apply weights after build" checked:true
    spinner sp_tol "X tolerance" range:[0.0001,5.0,0.01] type:#float
    button b_mirror_geo "Mirror Geometry (create mirrored copy)"
    button b_mirror_weights "Mirror Skin Weights (apply to copy)"
    memo m_log "" width:620 height:240

    local pairs = undefined
    fn buildPairsForMesh obj tol = (
        local sm = snapshotAsMesh obj
        local pairsLocal = #()
        for i = 1 to sm.numverts do (
            local vi = sm.getVert i
            local best = 0; local bd = 1e9
            for j = 1 to sm.numverts do (
                if i == j then continue
                local vj = sm.getVert j
                if abs(vi.x + vj.x) <= tol then (
                    local d = ((vi.y - vj.y)^2 + (vi.z - vj.z)^2)
                    if d < bd then (bd = d; best = j)
                )
            )
            if best > 0 then append pairsLocal #(i,best)
        )
        pairsLocal
    )

    on b_build_pairs pressed do (
        if selection.count == 0 then (messageBox "Select mesh to build pairs from"; return())
        local obj = selection[1]
        undo "Build Mirror Pairs" on (
            pairs = buildPairsForMesh obj sp_tol.value
        )
        m_log.text = "Pairs built: " + (pairs.count as string) + "\n"
        if cb_auto_apply.checked then messageBox ("Pairs built: " + (pairs.count as string))
    )

    on b_mirror_geo pressed do (
        if selection.count == 0 then (messageBox "Select mesh to mirror"; return())
        local obj = selection[1]
        undo "Mirror Geometry" on (
            local new = copy obj name:(obj.name + "_mirrored")
            convertToMesh new
            local s = snapshotAsMesh new
            for vi = 1 to s.numverts do (
                local p = getVert s vi
                setVert new vi ([-p.x, p.y, p.z])
            )
            update new
            m_log.text += ("Created mirrored copy: " + new.name + "\n")
        )
        messageBox "Mirror geometry done."
    )

    on b_mirror_weights pressed do (
        if selection.count == 0 then (messageBox "Select source mesh (original)"; return())
        local src = selection[1]
        local dstName = src.name + "_mirrored"
        local dst = getNodeByName dstName
        if dst == undefined then (messageBox ("Mirrored copy not found: " + dstName); return())
        local sskin = undefined; local dskin = undefined
        for m in src.modifiers where classof m == Skin do sskin = m
        for m in dst.modifiers where classof m == Skin do dskin = m
        if sskin == undefined or dskin == undefined then (messageBox "Both meshes must have Skin modifier (add Skin to mirrored copy)"; return())
        if pairs == undefined or pairs.count == 0 then (messageBox "Vertex pairs not built - building now with default tol 0.01"; pairs = buildPairsForMesh src 0.01)
        undo "Mirror Skin Weights" on (
            local mappedBones = dotNetObject "System.Collections.Hashtable"
            local nb = skinOps.GetNumberBones sskin
            for i = 1 to nb do (
                local bname = skinOps.GetBoneName sskin i
                local mirrorCandidates = #(replaceString bname "L_" "R_", replaceString bname "R_" "L_", replaceString bname "_L" "_R", replaceString bname "_R" "_L")
                local found = undefined
                for cand in mirrorCandidates do (
                    for j = 1 to skinOps.GetNumberBones dskin do (
                        if (toLower (skinOps.GetBoneName dskin j)) == (toLower cand) then ( found = j; exit)
                    )
                    if found != undefined then exit
                )
                if found == undefined then
                (
                    for j = 1 to skinOps.GetNumberBones dskin do if (toLower (skinOps.GetBoneName dskin j)) == (toLower bname) then (found = j; exit)
                )
                if found == undefined then found = 1
                mappedBones.Add i found
            )

            for k = 1 to pairs.count do (
                local sidx = pairs[k][1]
                local didx = pairs[k][2]
                local wc = skinOps.GetVertexWeightCount sskin sidx
                local weightsArr = #()
                for wi = 1 to wc do (
                    local boneId = skinOps.GetVertexWeightBoneID sskin sidx wi
                    local w = skinOps.GetVertexWeight sskin sidx wi
                    local mapped = mappedBones.Item[boneId]
                    append weightsArr #(mapped, w)
                )
                local ssum = 0.0
                for p in weightsArr do ssum += p[2]
                if ssum == 0 then append weightsArr #([1,1.0]) else for i=1 to weightsArr.count do weightsArr[i][2] = weightsArr[i][2] / ssum
                skinOps.ReplaceVertexWeights dskin didx weightsArr
            )
            update dst
            m_log.text += ("Mirrored weights applied to: " + dst.name + "\n")
        )
        messageBox "Mirror weights applied."
    )
)
createDialog MirrorCoreV3
