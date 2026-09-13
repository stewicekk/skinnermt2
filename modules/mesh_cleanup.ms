-- mesh_cleanup.ms - Combine welds and remove degenerate faces
-- Generated: 2025-09-21

try(destroyDialog MeshCleanup)catch()
rollout MeshCleanup "Mesh Cleanup" width:420 height:180
(
    spinner sp_tol "Weld tolerance" range:[0.0001,0.1,0.001] type:#float
    button b_weld "Weld verts"
    button b_remove_deg "Remove degenerate faces"
    on b_weld pressed do (
        for o in selection do ( convertToMesh o; meshop.weldVertsByThreshold o sp_tol.value ); messageBox "Weld attempted." )
    on b_remove_deg pressed do (
        for o in selection do ( convertToMesh o; meshop.removeDegenerateFaces o ); messageBox "Degenerate removal attempted." )
)
createDialog MeshCleanup
