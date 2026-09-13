/* Generated: 2025-09-21 */


/* exporter_compatibility_scanner_v2.ms - Compatibility Scanner (IMPLEMENTED)
Scans selected objects for exporter compatibility (vertex count, bone count).
*/
try(destroyDialog CompatScan)catch()
rollout CompatScan "Compatibility Scanner v2" width:520 height:300
(
    button b_scan "Scan selection"
    memo m_report "" width:500 height:220
    on b_scan pressed do
    (
        m_report.text = ""
        for o in selection do
        (
            local txt = "Object: " + o.name + "\n"
            local m = snapshotAsMesh o
            txt += "Verts: " + (m.numverts as string) + ", Faces: " + (m.numfaces as string) + "\n"
            local sk = undefined; for mo in o.modifiers where classof mo==Skin do sk=mo
            if sk != undefined then txt += "Bones: " + (skinOps.GetNumberBones sk as string) + "\n"
            if m.numverts > 60000 then txt += "Warning: Vertex count > 60k\n"
            m_report.text += txt + "\n"
        )
        messageBox "Scan complete. See report."
    )
)
createDialog CompatScan
