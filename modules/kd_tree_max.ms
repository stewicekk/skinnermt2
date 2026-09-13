-- kd_tree_max.ms - Spatial hash helper for vertex pairing (MaxScript)\n-- Generated: 2025-09-21\n\n
fn vecToKey v voxel = (
    local vx = floor (v.x / voxel)
    local vy = floor (v.y / voxel)
    local vz = floor (v.z / voxel)
    vx as string + "_" + vy as string + "_" + vz as string
)

fn buildSpatialHashPairs meshObj voxel tol = (
    local sm = snapshotAsMesh meshObj
    local n = sm.numverts
    local hash = dotNetObject "System.Collections.Hashtable"
    for i = 1 to n do (
        local v = sm.getVert i
        local k = vecToKey v voxel
        if hash.ContainsKey k then hash.Item[k] = append hash.Item[k] i else hash.Add k, #(i)
    )
    local pairs = #()
    for i = 1 to n do (
        local vi = sm.getVert i
        local target = -vi.x
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
        if found != undefined then append pairs #(i, found)
    )
    pairs
)

-- Export vertex positions to JSON for external pairing (Python)
fn exportVertexPositions meshObj outFile = (
    local sm = snapshotAsMesh meshObj
    local verts = #()
    for i = 1 to sm.numverts do (
        local v = sm.getVert i
        append verts (array v.x v.y v.z)
    )
    -- write to file
    try (
        local fh = createFile outFile
        format "{\\n  \\"name\\": \\"%\\",\\n  \\"vertices\\": [\\n" meshObj.name to:fh
        for i = 1 to verts.count do (
            local v = verts[i]
            format "    [%,%,%]" v[1] v[2] v[3] to:fh
            if i < verts.count then format ",\\n" to:fh else format "\\n" to:fh
        )
        format "  ]\\n}\\n" to:fh
        close fh
        messageBox ("Exported vertex positions to: " + outFile)
    ) catch (messageBox "Failed to write file")
)

-- Import pairs JSON (format: [[srcIdx,dstIdx],...])
fn importPairsFromJSON inFile = (
    try (
        local fh = openFile inFile
        local txt = readLine fh
        local full = txt
        while not eof fh do full += readLine fh
        close fh
        -- quick parse by scanning for numbers
        local nums = #()
        for c in full do (
            if (findString "0123456789-," c) > 0 then append nums c else append nums "|"
        )
        -- fallback: write imported content to log and notify user to manually verify
        local logfn = getDir #userScripts + "\\\\imported_pairs_raw.txt"
        local fhw = createFile logfn
        format "%\\n" full to:fhw
        close fhw
        messageBox ("Imported raw pairs content (saved to: " + logfn + "). Please verify and convert to MaxScript array if needed.")
        full
    ) catch (messageBox "Failed to import pairs file")
)

-- Example function: create mirrored copy and apply pairs (calls internal buildSpatialHashPairs)
fn mirrorAndApplyWeights src voxel tol dryrun:false =
(
    local pairs = buildSpatialHashPairs src voxel tol
    local dst = copy src name:(src.name + "_mirrored")
    convertToMesh dst
    local s = snapshotAsMesh dst
    for i = 1 to s.numverts do (
        local p = getVert s i
        setVert dst i ([-p.x, p.y, p.z])
    )
    update dst
    -- Note: weights application must be done by skinOps from caller; here we only return pairs and dst node
    pairs, dst
)
