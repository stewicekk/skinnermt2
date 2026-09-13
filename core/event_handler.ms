global M2Events

struct M2EventsStruct
(
    listeners = dotNetObject "System.Collections.Hashtable",

    fn subscribe evt fnPtr =
    (
        if evt == undefined or fnPtr == undefined then return false
        if not listeners.ContainsKey evt then listeners.Add evt (dotNetObject "System.Collections.ArrayList")
        listeners[evt].Add fnPtr
        true
    ),

    fn unsubscribe evt fnPtr =
    (
        if not listeners.ContainsKey evt then return false
        try ( listeners[evt].Remove fnPtr; true ) catch ( false )
    ),

    fn publish evt payload =
    (
        if not listeners.ContainsKey evt then return undefined
        local arr = listeners[evt]
        for i = 0 to arr.Count-1 do
        (
            try
            (
                local fnPtr = arr[i]
                fnPtr payload
            )
            catch
            (
                -- Log the error to MaxScript listener but continue
                format "Event listener % failed for event %: %\n" arr[i] evt (getCurrentException())
            )
        )
    )
)

global M2Events = M2EventsStruct()
