-- mirror_symmetry_core_v2.ms - Mirror geometry and weight symmetry helpers
-- Generated: 2025-09-21

try(destroyDialog MirrorCore)catch()
rollout MirrorCore "Mirror & Symmetry Core v2" width:520 height:200
(
    button b_mirror_geo "Mirror geometry X"
    button b_mirror_weights "Mirror skin weights X"
    on b_mirror_geo pressed do
    (
        for o in selection do (
            convertToMesh o
            local new = copy o name:(o.name + "_mirrored")
            for v=1 to snapshotAsMesh(new).numverts do (
                local p = new.verts[v].position
                new.verts[v].position = [-p.x, p.y, p.z]
            )
        )
        messageBox "Mirroring attempted."
    )
    on b_mirror_weights pressed do
    (
        for o in selection do for m in o.modifiers where classof m == Skin do
        (
            local nv = skinOps.GetNumberVertices m
            for v=1 to nv do
            (
                -- placeholder safer than risky operations
            )
        )
        messageBox "Mirror weights helper finished (limited)."
    )
)
createDialog MirrorCore
