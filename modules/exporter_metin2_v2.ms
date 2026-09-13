/* Generated: 2025-09-21 */


/* exporter_metin2_v2.ms - MDE JSON exporter (IMPROVED)
Exports vertex positions, faces, top-N weights per vertex and generates a meta JSON.
*/
try(destroyDialog ExporterM2V2)catch()
rollout ExporterM2V2 "Metin2 Exporter v2 (IMPROVED)" width:600 height:340
(
    spinner sp_top "Top-N weights" range:[1,8,4] type:#integer
    button b_export "Export selected to MDE-like JSON + .meta"
    memo m_log "" width:580 height:240
    on b_export pressed do
    (
        m_log.text = ""
        if selection.count==0 then (messageBox "Select mesh") else
        (
            local obj = selection[1]
            local out = getSaveFileName caption:"Export JSON" filename:(obj.name + "_mde.json")
            if out==undefined then return()
            local folder = getFilenamePath out
            local sk = undefined; for mo in obj.modifiers where classof mo==Skin do sk=mo
            if sk==undefined then (messageBox "Selected object has no Skin modifier"); return()
            local mesh = snapshotAsMesh obj
            local f = createFile out
            format "{\n  \"name\": \"%\",\n" obj.name to:f
            format "  \"vertices\":[\n" to:f
            for v=1 to mesh.numverts do ( local p = mesh.getVert v; format "    [%,%,%]" p.x p.y p.z to:f; if v<mesh.numverts then format ",\n" to:f else format "\n" to:f )
            format "  ],\n  \"faces\":[\n" to:f
            for fi=1 to mesh.numfaces do ( local t=getFace mesh fi; format "    [%,%,%]" t.x t.y t.z to:f; if fi<mesh.numfaces then format ",\n" to:f else format "\n" to:f )
            format "  ],\n  \"weights\":[\n" to:f
            for v=1 to mesh.numverts do
            (
                local arr = #()
                local wc = skinOps.GetVertexWeightCount sk v
                for i=1 to wc do append arr #(skinOps.GetVertexWeightBoneID sk v i, skinOps.GetVertexWeight sk v i)
                qsort arr (fn A B -> A[2] > B[2])
                format "    [" to:f
                for i=1 to (min sp_top.value arr.count) do ( format "[%,%]" arr[i][1] arr[i][2] to:f; if i < (min sp_top.value arr.count) then format "," to:f )
                format "]" to:f
                if v<mesh.numverts then format ",\n" to:f else format "\n" to:f
            )
            format "  ]\n}}\n" to:f; close f
            m_log.text += ("Exported MDE JSON: " + out + "\n")
            -- write meta
            local metaPath = folder + obj.name + "_meta.json"
            local mf = createFile metaPath
            format "{\n  \"object\": \"%\",\n  \"vertices\": %,\n  \"faces\": %,\n  \"topN\": %\n}\n" obj.name mesh.numverts mesh.numfaces sp_top.value to:mf
            close mf
            m_log.text += ("Wrote meta: " + metaPath + "\n")
            messageBox ("Export complete. See log.")
        )
    )
)
createDialog ExporterM2V2
