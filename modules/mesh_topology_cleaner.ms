/* Generated: 2025-09-21 */


/* mesh_topology_cleaner.ms - Topology Cleaner (IMPLEMENTED)
Recompute normals and remove isolated verts.
*/ 
try(destroyDialog TopoClean)catch()
rollout TopoClean "Topology Cleaner" width:420 height:220
(
    button b_fixnorm "Recompute normals"
    button b_removeiso "Remove isolated verts"
    on b_fixnorm pressed do ( for o in selection do ( convertToMesh o; meshop.makeNormals o ); messageBox "Normals recomputed." )
    on b_removeiso pressed do
    (
        for o in selection do
        (
            local m = snapshotAsMesh o
            local used = #()
            for f=1 to m.numfaces do ( local t = getFace m f; used[t.x]=true; used[t.y]=true; used[t.z]=true )
            local del = #()
            for v=1 to m.numverts do if used[v]==undefined then append del v
            if del.count>0 then meshop.deleteVerts o del
        )
        messageBox "Isolated verts removed."
    )
)
createDialog TopoClean
