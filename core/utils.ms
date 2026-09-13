-- utils.ms - Utility helpers for Metin2 SkinRig (v12)
-- Generated: 2025-09-21


fn readFileAll path = (
    try (
        local fh = openFile path
        local s = ""
        while not eof fh do s += (readLine fh + "\n")
        close fh
        s
    ) catch ( undefined )
)

fn writeFileAll path txt = (
    try (
        local fh = createFile path
        format "%\n" txt to:fh
        close fh
        true
    ) catch ( false )
)

fn getNodeByNameCaseInsensitive name = (
    for n in geometry where (toLower n.name) == (toLower name) do return n
    for n in helpers where (toLower n.name) == (toLower name) do return n
    undefined
)

global MSLogger = (if MSLogger == undefined then (struct MSLogger (path) ) else MSLogger)
fn logAppend txt = (
    try (
        local p = if MSLogger.path == undefined then (getDir #userScripts + "\\metin2_log.txt") else MSLogger.path
        local fh = appendFile p
        format "% [%] %\n" (timestamp()) (txt) to:fh
        close fh
    ) catch ()
)

fn safeSnapshotAsMesh obj = (
    try ( snapshotAsMesh obj ) catch ( undefined )
)
