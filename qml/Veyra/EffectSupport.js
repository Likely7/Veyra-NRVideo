.pragma library

function status(api, id) {
    // Reading the notifying property also invalidates bindings after settings,
    // adapter/runtime status or a rejected backend request changes.
    var caps = api.effectCapabilities
    return typeof api.effectAvailability === "function"
            ? api.effectAvailability(id) : {available: true, reason: ""}
}
function available(api, id) { return status(api, id).available }
function reason(api, id) { return status(api, id).reason }
function option(api, id, label, key) {
    var cap = (api.effectCapabilities || {})[key] || {available: true, reason: ""}
    return {id: id, label: label, disabled: !cap.available, note: cap.reason}
}
