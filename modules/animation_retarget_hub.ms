-- animation_retarget_hub.ms - Hub to open retarget tools
-- Generated: 2025-09-21
try(destroyDialog RetargetHub)catch()
rollout RetargetHub "Retarget Hub" width:360 height:140
(
    button b_open_auto "Open Auto Retarget v2"
    button b_open_v4 "Open Auto Retarget v4"
    on b_open_auto pressed do ( try(fileIn (GetDir #scripts + "\\auto_retarget_v2.ms")) catch (messageBox "auto_retarget_v2.ms not found") )
    on b_open_v4 pressed do ( try(fileIn (GetDir #scripts + "\\ai_retarget_v4.ms")) catch (messageBox "ai_retarget_v4.ms not found") )
)
createDialog RetargetHub
