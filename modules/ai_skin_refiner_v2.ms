/* Generated: 2025-09-21 */


/* ai_skin_refiner_v2.ms - AI SkinRefiner v2 (IMPROVED)
Connects to local AI backend at /ai_skin and supports /ai_update fine-tuning.
Includes API-Key header support and preview UI.
*/
try(destroyDialog AISkinRefinerV2)catch()
rollout AISkinRefinerV2 "AI SkinRefiner v2 (IMPROVED)" width:640 height:420
(
    editText et_endpoint "AI endpoint" text:"http://127.0.0.1:9000/ai_skin" width:600
    editText et_apikey "API Key (optional)" text:"" width:600
    spinner sp_top "Top-N apply" range:[1,8,4] type:#integer
    button b_fetch "Fetch suggestion"
    button b_apply "Apply suggestion (to selected mesh)"
    button b_send_update "Send corrected weights to /ai_update (fine-tune)"
    listbox lb_preview "" width:600 height:120 items:#()
    local predictions = undefined
    fn httpPostJson(url,jsonStr, apiKey) = (
        local wc = dotNetObject "System.Net.WebClient"
        wc.Headers.Add "Content-Type","application/json"
        if apiKey != undefined and apiKey != "" then wc.Headers.Add "X-API-KEY", apiKey
        local res = wc.UploadString url "POST" jsonStr
        res
    )
    on b_fetch pressed do
    (
        if selection.count==0 then (messageBox "Select skinned mesh"); return()
        local obj = selection[1]; local sk = undefined
        for m in obj.modifiers where classof m==Skin do sk=m
        if sk==undefined then (messageBox "No Skin modifier on selection"); return()
        local mmesh = snapshotAsMesh obj
        local verts = for v=1 to mmesh.numverts collect ( #(mmesh.getVert v.x, mmesh.getVert v.y, mmesh.getVert v.z) )
        local serializer = dotNetObject "System.Web.Script.Serialization.JavaScriptSerializer"
        local bones = for i=1 to skinOps.GetNumberBones sk collect skinOps.GetBoneName sk i
        local payload = dotNetObject "System.Collections.Hashtable"
        payload.Add "bones" bones
        payload.Add "vertices" verts
        local json = serializer.Serialize payload
        try (
            local res = httpPostJson et_endpoint.text json et_apikey.text
            local out = serializer.DeserializeObject res
            predictions = out["weights"]
            lb_preview.items = #("Received " + (predictions.count as string) + " vertex predictions")
            messageBox ("Received predictions for " + obj.name)
        ) catch ( messageBox ("AI fetch failed: " + (getCurrentException())) )
    )
    on b_apply pressed do
    (
        if predictions==undefined then (messageBox "No predictions stored. Fetch first."); return()
        if selection.count==0 then (messageBox "Select skinned mesh"); return()
        local obj = selection[1]; local sk = undefined
        for m in obj.modifiers where classof m==Skin do sk=m
        if sk==undefined then (messageBox "No Skin modifier"); return()
        for v=1 to predictions.count do
        (
            local arr = predictions[v]
            local pairs = #()
            for i=1 to arr.count do if arr[i] > 1e-6 do append pairs #([i, arr[i]])
            qsort pairs (fn A B -> A[2] > B[2])
            local apply = for i=1 to (min sp_top.value pairs.count) collect pairs[i]
            local s=0.0; for p in apply do s+=p[2]
            if s==0 then s=1.0
            for i=1 to apply.count do apply[i][2] = apply[i][2]/s
            skinOps.ReplaceVertexWeights sk v apply
        )
        update obj; messageBox "Predictions applied."
    )
    on b_send_update pressed do
    (
        if selection.count==0 then (messageBox "Select skinned mesh"); return()
        local obj = selection[1]; local sk = undefined
        for m in obj.modifiers where classof m==Skin do sk=m
        if sk==undefined then (messageBox "No Skin modifier"); return()
        local mmesh = snapshotAsMesh obj
        local verts = for v=1 to mmesh.numverts collect ( #(mmesh.getVert v.x, mmesh.getVert v.y, mmesh.getVert v.z) )
        local weights = #()
        local bones = for i=1 to skinOps.GetNumberBones sk collect skinOps.GetBoneName sk i
        for v=1 to mmesh.numverts do
        (
            local wc = skinOps.GetVertexWeightCount sk v
            local arr = for i=1 to wc collect skinOps.GetVertexWeight sk v i
            while arr.count < bones.count do append arr 0.0
            append weights arr
        )
        local serializer = dotNetObject "System.Web.Script.Serialization.JavaScriptSerializer"
        local payload = dotNetObject "System.Collections.Hashtable"
        payload.Add "bones" bones
        payload.Add "vertices" verts
        payload.Add "weights" weights
        local json = serializer.Serialize payload
        try (
            local updEndpoint = et_endpoint.text
            if findString updEndpoint "/ai_skin" > 0 then updEndpoint = replaceString updEndpoint "/ai_skin" "/ai_update"
            local res = httpPostJson updEndpoint json et_apikey.text
            messageBox ("AI update response: " + res)
        ) catch ( messageBox ("AI update failed: " + (getCurrentException())) )
    )
)
createDialog AISkinRefinerV2
