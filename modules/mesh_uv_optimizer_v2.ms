/* Generated: 2025-09-21 */


/* mesh_uv_optimizer_v2.ms - UV Optimizer v2 (BASIC)
Simple wrapper for Unwrap UVW pack tools.
*/
try(destroyDialog UVOptV2)catch()
rollout UVOptV2 "UV Optimizer v2" width:480 height:200
(
    spinner sp_margin "Margin px" range:[0,64,4] type:#integer value:4
    button b_pack "Pack UVs (basic)"
    on b_pack pressed do
    (
        for obj in selection do
        (
            try ( unwrap_uvw.reset obj ) catch()
            try ( unwrap_uvw.pack obj spacing:sp_margin.value ) catch()
        )
        messageBox "Basic UV pack attempted."
    )
)
createDialog UVOptV2
