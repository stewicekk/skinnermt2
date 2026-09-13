-- lod_simplifier.ms - Create LOD via ProOptimizer or poly reduce
-- Generated: 2025-09-21

try(destroyDialog LODSim)catch()
rollout LODSim "LOD Simplifier" width:520 height:220
(
    spinner sp_levels "LOD levels" range:[1,6,3] type:#integer
    spinner sp_ratio "Reduce % per level" range:[5,90,40] type:#integer
    button b_gen "Generate LODs"
    memo m_log "" width:500 height:140
    on b_gen pressed do
    (
        if selection.count == 0 then (messageBox "Select base mesh"); return()
        local base = selection[1]
        m_log.text = "Starting LOD generation...\n"
        for lvl = 1 to sp_levels.value do
        (
            local copyObj = copy base name:(base.name + "_LOD" + (lvl as string))
            try (
                local po = ProOptimizer()
                addModifier copyObj po
                po.VertexPercent = max 1 (100 - (lvl * sp_ratio.value))
                po.calculate = true
                m_log.text += ("Created: " + copyObj.name + "\n")
            ) catch (
                m_log.text += ("ProOptimizer missing, skipping: " + copyObj.name + "\n")
                try ( meshop.reduce copyObj (sp_ratio.value) ) catch()
            )
        )
        messageBox "LOD generation done. See log."
    )
)
createDialog LODSim
