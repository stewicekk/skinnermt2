-- mesh_effect_hooks.ms - Simple hooks to run code after mesh ops
-- Generated: 2025-09-21

try(destroyDialog MeshHooks)catch()
rollout MeshHooks "Mesh Effect Hooks" width:420 height:160
(
    button b_register "Register sample hook"
    on b_register pressed do
    (
        registerEvent "afterMeshOp" (fn args -> format "afterMeshOp called: %\n" args)
        messageBox "Sample hook registered."
    )
)
createDialog MeshHooks
