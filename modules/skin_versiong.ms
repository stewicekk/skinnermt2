-- skin_versiong.ms - Manage skin script versioning and changelog embed
-- Generated: 2025-09-21

try(destroyDialog SkinVersion)catch()
rollout SkinVersion "Skin Version" width:420 height:160
(
    button b_show "Show version"
    on b_show pressed do ( messageBox "Metin2 Skin & Rig - v12.0 (generated)" )
)
createDialog SkinVersion
