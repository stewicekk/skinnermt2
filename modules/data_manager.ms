-- data_manager.ms - Save/load simple JSON metadata for objects
-- Generated: 2025-09-21
try(destroyDialog DataManager)catch()
rollout DataManager "Data Manager" width:520 height:200
(
    button b_save "Save selected metadata"
    button b_load "Load metadata to selected"
    on b_save pressed do
    (
        if selection.count == 0 then (messageBox "Select object to save metadata"; return())
        local obj = selection[1]
        local out = getSaveFileName caption:"Save metadata" filename:(obj.name + "_meta.json")
        if out == undefined then return()
        local fh = createFile out
        format "{\n  \"object\": \"%\",\n  \"pos\": [%,%,%]\n}\n" obj.name obj.pos.x obj.pos.y obj.pos.z to:fh
        close fh
        messageBox ("Saved metadata to " + out)
    )
    on b_load pressed do
    (
        if selection.count == 0 then (messageBox "Select object to apply metadata"; return())
        local infile = getOpenFileName caption:"Load metadata"
        if infile == undefined then return()
        try (
            local fh = openFile infile
            local txt = readFile fh
            close fh
            messageBox ("Loaded metadata:\n" + txt)
        ) catch (messageBox "Failed to read metadata file")
    )
)
createDialog DataManager
