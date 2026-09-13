global M2Data

struct M2DataStruct
(
    basePath = getDir #scripts + "\\Metin2_SkinRig_v11_6_final_full\\",
    fn resolve path = ( if path == undefined then basePath else path ),
    fn readText path =
    (
        if not doesFileExist path then undefined
        local s = ""; local f = openFile path mode:"rt"
        while not eof f do s += readLine f + "\n"
        close f; s
    ),
    fn writeText path txt =
    (
        local dir = getFilenamePath path
        if dir != "" and not doesDirectoryExist dir then makeDir dir
        local f = createFile path
        format "%\n" txt to:f
        close f
        true
    ),
    fn savePreset name jsonTxt =
    (
        local p = basePath + "extras/presets/" + name + ".json"
        local dir = getFilenamePath p
        if not doesDirectoryExist dir then makeDir dir
        writeText p jsonTxt
    ),
    fn loadPreset name =
    (
        local p = basePath + "extras/presets/" + name + ".json"
        readText p
    ),
    fn listPresets =
    (
        local p = basePath + "extras/presets/"
        if not doesDirectoryExist p then #()
        else ( local files = getFiles (p + "*.json"); for f in files collect getFilenameFile f )
    )
)

global M2Data = M2DataStruct()
