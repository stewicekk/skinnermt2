rollout M2SR_core "Metin2 Skin & Rig — Core Launcher" width:520 height:220
(
    label lbl_title "Metin2 Skin & Rig v11.6 — CORE" align:#center across:2
    button b_open_ui "Open Main UI" width:200 align:#center tooltip:"Launch the main UI manager (modules/ui_manager.ms)"
    button b_check "Run Integrity Check" width:200 align:#center tooltip:"Check installation and report missing files"
    label lbl_status "" align:#left width:480 height:40
    button b_open_readme "Open README_FULL" width:200 align:#center tooltip:"Open full README (notepad)"
    on b_open_ui pressed do
    (
        local scriptsPath = getDir #scripts
        local uiPath = scriptsPath + "\\Metin2_SkinRig_v11_6_final_full\\modules\\ui_manager.ms"
        if doesFileExist uiPath then
        (
            try(fileIn uiPath)catch(messageBox ("Failed to run UI manager. Error: " + (getCurrentException())))
        )
        else
        (
            messageBox ("UI manager not found at:\n" + uiPath + "\n\nPlease ensure you've unpacked the ZIP into your 3ds Max scripts folder. See installation docs in extras/docs/")
        )
    )
    on b_open_readme pressed do
    (
        local scriptsPath = getDir #scripts
        local readmePath = scriptsPath + "\\Metin2_SkinRig_v11_6_final_full\\extras\\docs\\README_FULL.txt"
        if doesFileExist readmePath then shellLaunch "notepad" readmePath else messageBox ("README_FULL.txt not found. Expected path:\n" + readmePath)
    )
    fn checkFile f = ( if doesFileExist f then format "OK: %\n" f to:stdout else format "MISSING: %\n" f to:stdout )
    on b_check pressed do
    (
        lbl_status.text = "Running integrity check..."
        local scriptsPath = getDir #scripts
        local base = scriptsPath + "\\Metin2_SkinRig_v11_6_final_full\\"
        local required = #(
            base + "core\\core_loader.ms",
            base + "core\\event_handler.ms",
            base + "core\\data_manager.ms",
            base + "core\\utils.ms",
            base + "modules\\ui_manager.ms",
            base + "extras\\docs\\installation.txt",
            base + "ai_backend\\server.py"
        )
        local missing = #()
        for f in required do (
            if not doesFileExist f then append missing f
        )
        if missing.count == 0 then
        (
            lbl_status.text = "Integrity OK — all required core files found."
            messageBox "Integrity check passed. You have core files installed."
        )
        else
        (
            lbl_status.text = ("Missing files: " + missing.count as string)
            local s = "Missing files:\n"
            for m in missing do s += m + "\n"
            messageBox s title:"Integrity check - missing files"
        )
    )
)
createDialog M2SR_core
