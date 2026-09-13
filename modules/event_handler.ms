-- event_handler.ms - simple event bus
-- Generated: 2025-09-21

global EventBus = (if EventBus == undefined then (struct EventBusClass (handlers) ) else EventBus)
EventBus.handlers = if EventBus.handlers == undefined then #() else EventBus.handlers

fn registerEvent name fnRef = (
    append EventBus.handlers #(name, fnRef)
)

fn emitEvent name args = (
    for h in EventBus.handlers where h[1] == name do (
        try (h[2] args) catch()
    )
)

-- example registration
-- registerEvent "onExport" (fn args -> format "Export event: %\n" args)
