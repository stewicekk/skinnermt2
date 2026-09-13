-- exporter_mde.ms - Older MDE exporter (basic)
-- Generated: 2025-09-21

try(destroyDialog ExporterMDE)catch()
rollout ExporterMDE "Exporter MDE (legacy)" width:520 height:220
(
    button b_export "Export selected to mde legacy"
    on b_export pressed do
    (
        if selection.count == 0 then (messageBox "Select mesh"); return()
        local obj = selection[1]
        local out = getSaveFileName caption:"Export mde" filename:(obj.name + "_legacy_mde.json")
        if out == undefined then return()
        local m = snapshotAsMesh obj
        local f = createFile out
        format "{\n  \"name\": \"%\",\n  \"verts\": [\n" obj.name to:f
        for i=1 to m.numverts do (
            local v = m.getVert i
            format "    [%,%,%]" v.x v.y v.z to:f
            if i < m.numverts then format ",\n" to:f else format "\n" to:f
        )
        format "  ]\n}\n" to:f
        close f
        messageBox "Legacy export done."
    )
)
createDialog ExporterMDE
