-- metin2_compat_scanner.ms - Metin2 specific checks (texture sizes, bone names)
-- Generated: 2025-09-21

try(destroyDialog M2Compat)catch()
rollout M2Compat "Metin2 Compat Scanner" width:520 height:260
(
    button b_scan "Scan selection"
    memo m_out "" width:500 height:200
    on b_scan pressed do
    (
        m_out.text = ""
        for o in selection do
        (
            local line = o.name + ": "
            if o.material != undefined then
            (
                line += "Material present. "
            )
            local sk = undefined; for mo in o.modifiers where classof mo == Skin do sk = mo
            if sk != undefined then (
                local nb = skinOps.GetNumberBones sk
                line += (nb as string) + " bones. "
            )
            m_out.text += line + "\n"
        )
        messageBox "Scan complete. See report."
    )
)
createDialog M2Compat
