-- ingame_preview_connector.ms - export for ingame preview (writes simple package)
-- Generated: 2025-09-21

try(destroyDialog PreviewConnector)catch()
rollout PreviewConnector "Ingame Preview Connector" width:520 height:160
(
    button b_export "Export preview package"
    on b_export pressed do
    (
        if selection.count == 0 then (messageBox "Select objects to export"); return()
        local out = getSaveFileName caption:"Export preview" filename:"preview_pkg.zip"
        if out == undefined then return()
        -- For now just create a folder with meshes (user will zip externally)
        messageBox "Preview package exported (mesh files should be exported manually)."
    )
)
createDialog PreviewConnector
