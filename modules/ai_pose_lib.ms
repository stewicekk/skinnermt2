-- ai_pose_lib.ms - Pose utilities library
-- Generated: 2025-09-21

global PoseLib = (if PoseLib == undefined then #() else PoseLib)

fn getBoneWorldPos bone = (
    try (bone.pos) catch (undefined)
)

fn findNearestVertex mesh worldPos = (
    local m = snapshotAsMesh mesh
    local best = 1
    local bd = 1e9
    for i = 1 to m.numverts do (
        local v = m.getVert i
        local d = (v - worldPos).length
        if d < bd then (bd = d; best = i)
    )
    return best
)

fn meshVertexToWorld mesh idx = (
    local tm = mesh.transform
    local m = snapshotAsMesh mesh
    local v = m.getVert idx
    return (v * tm)
)

-- small test function
fn poseLib_test = (
    format "PoseLib OK\n"
)
