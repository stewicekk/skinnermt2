-- profiler.ms - Simple timer profiler
-- Generated: 2025-09-21

try(destroyDialog ProfilerDlg)catch()
rollout ProfilerDlg "Profiler" width:320 height:140
(
    button b_start "Start"
    button b_stop "Stop"
    local t0 = undefined
    on b_start pressed do ( t0 = (timestamp()); messageBox "Profiler started" )
    on b_stop pressed do (
        if t0 == undefined then (messageBox "Start first") else (
            local dt = (timestamp() - t0) as float / 1000.0
            messageBox ("Elapsed: " + (formattedPrint dt format:"fixed:2") + " s")
        )
    )
)
createDialog ProfilerDlg
