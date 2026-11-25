function physical(spec) { return spec.split(":")[0] }
function caption(id) {
    if (!id) return ""
    const [key, gesture] = id.split(":")
    const words = key.replace(/^Key(?=[A-Z]$)|^Digit(?=[0-9]$)/, "")
        .replace(/_/g, " ").replace(/([a-z])([A-Z])/g, "$1 $2").replace(/^./, c => c.toUpperCase())
    return gesture ? words + " · " + gesture : words
}
function label(labels, fallback) {
    if (!labels) return fallback
    if (typeof labels === "string") return labels
    const locale = (Qt.uiLanguage || "en").replace("_", "-")
    return labels[locale] || labels[locale.split("-")[0]] || labels.en || fallback
}
function situationName(profile, id) {
    const names = {on_foot: qsTr("步行"), aiming: qsTr("瞄准"), driving: qsTr("驾驶"),
                   flying: qsTr("飞行"), menu: qsTr("游戏菜单"), map: qsTr("地图"), system_nav: qsTr("主机菜单")}
    return label((profile.context_labels || {})[id], names[id] || caption(id))
}
function detectionSources(profile, situation) {
    const rules = (profile.recognition_rules || []).filter(rule => rule.target === situation)
    const conditions = [].concat(...rules.map(rule => rule.conditions || []))
    const sources = []
    if (conditions.some(check => ["trigger", "seen_trigger"].includes(check.op)))
        sources.push(qsTr("自适应扳机"))
    if (rules.some(rule => rule.start?.op === "stream_return") ||
            conditions.some(check => check.op === "packet" ||
                    (["pulses", "quiet", "available"].includes(check.op) && (check.source || "haptics") === "haptics")))
        sources.push(qsTr("触觉反馈"))
    if (!sources.length && Object.values(profile.auto_context_from_feedback || {})
            .some(rule => (typeof rule === "string" ? rule : rule.context) === situation))
        sources.push(qsTr("手柄反馈"))
    if (!sources.length && rules.length) sources.push(qsTr("识别规则"))
    return sources
}
function situationInputs(profile) {
    const inputs = Object.create(null)
    function add(target, name) {
        const names = inputs[target] || (inputs[target] = [])
        if (!names.includes(name)) names.push(name)
    }
    const shortcuts = Object.values(profile.context_hotkeys || {})
    for (const from of profile.contexts || []) {
        const rows = records(profile, from)
        const effective = table(profile, from)
        const specs = Object.keys(effective).filter(spec => !shortcuts.includes(physical(spec)))
        for (const change of profile.context_transitions || []) {
            if (change.from !== from || !specs.some(spec => effective[spec] === change.on)) continue
            const row = rows.find(binding => binding.action === change.on)
            add(change.to, row ? row.shortTitle : label(profile.outputs?.[change.on]?.label, caption(change.on)))
        }
        // A held situation binding can have no controller output of its own.
        for (const spec of specs) {
            const [key, gesture] = spec.split(":")
            if (gesture && gesture.startsWith("layer=")) {
                const target = gesture.slice(6)
                if (target !== from) add(target, qsTr("按住 %1").arg(inputName(profile, key)))
            }
        }
    }
    return inputs
}
function controlMark(id) {
    return ({triangle:"△", square:"□", circle:"○", cross:"×", touchpad:"TP", options:"OPT",
             dpad_up:"↑", dpad_down:"↓", dpad_left:"←", dpad_right:"→",
             left_stick:"LS", right_stick:"RS", share:"Create"})[id] || (id.length < 5 ? id.toUpperCase() : "")
}
function keycap(id) {
    if (inactiveLegends[id]) return inactiveLegends[id]
    if (id.startsWith("Numpad")) return id.slice(6)
    return ({Escape:"Esc", ArrowUp:"↑", ArrowDown:"↓", ArrowRight:"→", ArrowLeft:"←",
             ShiftLeft:"Shift", ShiftRight:"Shift", ControlLeft:"Ctrl", ControlRight:"Ctrl",
             AltLeft:"Alt", AltRight:"Alt", CapsLock:"Caps Lock", Insert:"Ins",
             Delete:"Del", PageUp:"PgUp", PageDown:"PgDn"})[id] || caption(id)
}
function table(profile, scene) {
    const base = profile.bindings || {}
    if (!scene) return Object.assign({},base)
    const overrides = (profile.bindings_per_context || {})[scene] || {}
    const keys = Object.keys(overrides).map(physical)
    const result = {}
    for (let spec of Object.keys(base))
        if (keys.indexOf(physical(spec)) < 0) result[spec] = base[spec]
    return Object.assign(result,overrides)
}
function inputName(profile, spec) {
    const [key, gesture] = spec.split(":")
    const side = key === "Mouse4" || key === "Mouse5"
    const name = ({MouseLeft: qsTr("鼠标左键"), MouseRight: qsTr("鼠标右键"),
                   MouseMiddle: qsTr("鼠标中键"), WheelUp: qsTr("滚轮向上"), WheelDown: qsTr("滚轮向下")})[key]
                 || (side ? qsTr("侧键 %1").arg(key.slice(-1)) : caption(key))
    if (gesture && gesture.startsWith("layer="))
        return qsTr("按住 %1 · %2").arg(name).arg(situationName(profile, gesture.slice(6)))
    return gesture ? name + " · " + gestureName(profile, spec) : name
}
function gestureName(profile, spec) {
    const gesture = spec.split(":")[1]
    if (!gesture) return qsTr("按下")
    if (gesture.startsWith("layer=")) return qsTr("按住") + " · " + situationName(profile, gesture.slice(6))
    return ({hold: qsTr("按住"), double: qsTr("双击"), toggle: qsTr("切换"), tap: qsTr("轻触")})[gesture] || gesture
}
function mouseMotion(profile, scene) {
    if (!profile.look || !scene || scene === "system_nav") return null
    const use = profile.look.per_context?.[scene]?.mouse_use || profile.look.mouse_use || "look"
    const control = ["steer", "move", "cursor"].includes(use) ? "left_stick" : "right_stick"
    const title = ({look: qsTr("环视"), aim: qsTr("瞄准"), steer: qsTr("转向"),
                    move: qsTr("移动"), cursor: qsTr("移动光标")})[use]
    return {spec: "MouseMotion", key: "MouseMotion", input: qsTr("鼠标移动"), fixed: true,
            title, shortTitle: title, controls: [control], output: controllerName(control)}
}
function controllerOutputs(profile, out) {
    const controls = [out.button, (out.trigger || {}).which,
                      out.left_stick && "left_stick", out.right_stick && "right_stick"].filter(Boolean)
    if (out.special === "weapon_wheel") {
        if (!out.button) controls.push("l1")
        controls.push("right_stick")
    } else if (out.special === "walk_modifier") {
        controls.push("left_stick")
    } else if (out.special) {
        controls.push(out.special)
    }
    const macro = (profile.macros || {})[out.macro]
    if (macro) {
        for (const step of macro.steps)
            controls.push(...(step.tap || step.press || step.release || []))
    }
    return [...new Set(controls)]
}
function records(profile, scene) {
    // An explicit console menu maps physical inputs straight to buttons.
    const menu = scene === "system_nav" && profile.console_menu
    const effective = menu || table(profile,scene)
    return Object.keys(effective).sort().map(spec => {
        const action = effective[spec], out = menu ? {button: action} : (profile.outputs || {})[action] || {}
        const controls = controllerOutputs(profile, out)
        const title = label(out.label,caption(action))
        const output = controls.map(caption).join(" + ")
        const suffix = " (" + output + ")"
        const shortTitle = title.endsWith(suffix) ? title.slice(0, -suffix.length) : title
        return {spec:spec, key:physical(spec), input:inputName(profile,spec), action:action, title:title, shortTitle:shortTitle, controls:controls, output:output}
    }).filter(r => !r.controls.includes("none") && !Object.values(profile.context_hotkeys || {}).includes(r.key))
}
function group(bindings,key) { return Object.keys(bindings).filter(s => physical(s) === key) }
function changesIn(profile,from,to) {
    return profile.contexts.filter(c => {
        const t = (profile.bindings_per_context || {})[c] || {}
        return !group(t,from).length || !group(t,to).length
    })
}
// Replace whole physical-input groups, just as the mapper overlays a situation.
// A gesture suffix remains attached to its action after a move or swap.
function move(profile,scene,spec,destination,choice) {
    const doc = JSON.parse(JSON.stringify(profile))
    const current = table(doc,scene), source = physical(spec)
    if (!current[spec] || !destination || destination === source || choice === "cancel") return doc
    const left = {}, right = {}
    for (const entry of group(current, source)) {
        if (choice === "swap" || entry === spec)
            right[destination + entry.substring(source.length)] = current[entry]
        else
            left[entry] = current[entry]
    }
    if (choice === "swap") {
        for (const entry of group(current, destination))
            left[source + entry.substring(destination.length)] = current[entry]
    }
    let target=doc.bindings
    if (scene) {
        if (!doc.bindings_per_context) doc.bindings_per_context={}
        target=doc.bindings_per_context[scene] || (doc.bindings_per_context[scene]={})
    }
    for (const key of [source,destination]) for (const k of group(target,key)) delete target[k]
    Object.assign(target,left,right)
    if (scene && !Object.keys(left).length && group(doc.bindings,source).length) {
        let none=Object.keys(doc.outputs).find(a => doc.outputs[a].special === "none")
        if (!none) {
            none = "veyra_unbound"
            while (doc.outputs[none]) none += "_"
            doc.outputs[none] = {special:"none"}
        }
        target[source]=none
    }
    return doc
}
// These positions exist on an ANSI keyboard but have no PhysKey in the mapper.
const inactiveLegends = {
    Backquote: "`", Minus: "-", Equal: "=", BracketLeft: "[", BracketRight: "]", Backslash: "\\",
    Semicolon: ";", Quote: "'", Comma: ",", Period: ".", Slash: "/",
    MetaLeft: "Win", MetaRight: "Win", ContextMenu: "Menu", PrintScreen: "PrtSc",
    ScrollLock: "ScrLk", Pause: "Pause", End: "End", NumLock: "Num",
    NumpadDivide: "/", NumpadMultiply: "*", NumpadSubtract: "−", NumpadAdd: "+",
    NumpadEnter: "Enter", NumpadDecimal: "."
}
const keyboard = makeKeyboard()
function makeKeyboard() {
    const keys = []
    // Rows use standard key pitches. Numbers are empty space; tuples are wide or tall keys.
    function row(y, x, entries) {
        for (const entry of entries) {
            if (typeof entry === "number") {
                x += entry
                continue
            }
            const sized = Array.isArray(entry)
            const key = sized ? entry[0] : entry
            const w = sized ? entry[1] : 1
            const h = sized && entry.length > 2 ? entry[2] : 1
            keys.push({ key, x, y, w, h, bindable: !inactiveLegends[key] })
            x += w
        }
    }
    const letters = word => word.split("").map(c => "Key" + c)
    const digits = "1234567890".split("").map(c => "Digit" + c)
    row(0, 0, ["Escape", 1, "F1", "F2", "F3", "F4", 0.5, "F5", "F6", "F7", "F8",
               0.5, "F9", "F10", "F11", "F12", 0.5, "PrintScreen", "ScrollLock", "Pause"])
    row(1.5, 0, ["Backquote", ...digits, "Minus", "Equal", ["Backspace", 2], 0.5,
                 "Insert", "Home", "PageUp", 0.5, "NumLock", "NumpadDivide", "NumpadMultiply", "NumpadSubtract"])
    row(2.5, 0, [["Tab", 1.5], ...letters("QWERTYUIOP"), "BracketLeft", "BracketRight", ["Backslash", 1.5],
                 0.5, "Delete", "End", "PageDown", 0.5, "Numpad7", "Numpad8", "Numpad9", ["NumpadAdd", 1, 2]])
    row(3.5, 0, [["CapsLock", 1.75], ...letters("ASDFGHJKL"), "Semicolon", "Quote", ["Enter", 2.25]])
    row(3.5, 19, ["Numpad4", "Numpad5", "Numpad6"])
    row(4.5, 0, [["ShiftLeft", 2.25], ...letters("ZXCVBNM"), "Comma", "Period", "Slash", ["ShiftRight", 2.75]])
    row(4.5, 16.5, ["ArrowUp", 1.5, "Numpad1", "Numpad2", "Numpad3", ["NumpadEnter", 1, 2]])
    row(5.5, 0, [["ControlLeft", 1.25], ["MetaLeft", 1.25], ["AltLeft", 1.25], ["Space", 6.25],
                 ["AltRight", 1.25], ["MetaRight", 1.25], ["ContextMenu", 1.25], ["ControlRight", 1.25], 0.5,
                 "ArrowLeft", "ArrowDown", "ArrowRight", 0.5, ["Numpad0", 2], "NumpadDecimal"])
    return keys
}
const mouse = ["MouseLeft","MouseMiddle","MouseRight","Mouse4","Mouse5","WheelUp","WheelDown"]
const extraKeys = Array.from({length: 12}, (_, i) => "F" + (i + 13))
// Centers and hit areas in the DualSense front elevation's 270 × 184 view box.
// Width and height define separate click targets for stick travel and stick press.
// Adapted 2026-11-25 from src/keys/ControllerDiagram.tsx at cc2e75f (AGPL-3.0-only).
const controllerPorts = {
    l2: [62, 8, 23, 8], l1: [46, 15, 23, 8], share: [74.1, 26.2, 11, 15],
    dpad_up: [53.5, 43.8, 15, 16], dpad_left: [41.8, 55.5, 16, 15],
    dpad_right: [65.2, 55.5, 16, 15], dpad_down: [53.5, 67.2, 15, 16],
    left_stick: [80, 91, 12, 22], l3: [94, 90.8, 16, 19],
    touchpad: [135, 36, 106, 58], ps: [135, 88.8, 22, 16],
    r2: [208, 8, 23, 8], r1: [224, 15, 23, 8], options: [195.9, 26.2, 11, 15],
    triangle: [216.5, 35.8, 18, 18], square: [197.4, 55.3, 18, 18],
    circle: [235.7, 55.3, 18, 18], cross: [216.5, 74.5, 18, 18],
    right_stick: [190, 91, 12, 22], r3: [176, 90.8, 16, 19]
}
// Some near edges are curved or angled. End the line on the visible part.
function controllerLeader(id, side) {
    const port = controllerPorts[id]
    const right = side === "right"
    switch (id) {
    case "l2": return [right ? 68 : 59, 6]
    case "r2": return [211, 6]
    case "l1": return [right ? 60 : 43, right ? 11 : 15]
    case "r1": return [227, 15]
    case "share": return [right ? 77 : 71, 26]
    case "options": return [199, 26]
    case "left_stick": return [right ? 114 : 72, 90.8]
    case "right_stick": return [198, 90.8]
    case "l3": return [right ? 105.5 : 80.9, 90.8]
    case "r3": return [189.1, 90.8]
    case "touchpad": return right ? [184, 36] : [135, 7]
    case "ps": return right ? [144, 92] : [135, 94]
    }
    return [port[0] + (right ? 0.5 : -0.5) * port[2], port[1]]
}
function controllerName(id) {
    if (/^[lr][123]$/.test(id)) return id.toUpperCase()
    const names = {
        dpad_up: qsTr("方向键上"), dpad_down: qsTr("方向键下"),
        dpad_left: qsTr("方向键左"), dpad_right: qsTr("方向键右"),
        left_stick: qsTr("左摇杆"), right_stick: qsTr("右摇杆"),
        triangle: qsTr("三角键"), square: qsTr("方块键"),
        circle: qsTr("圆圈键"), cross: qsTr("叉键"),
        share: qsTr("创建键"), options: qsTr("选项键"),
        touchpad: qsTr("触摸板"), ps: "PS"
    }
    return names[id] || caption(id)
}
function controllerGroups(rows, control) {
    const groups = new Map()
    for (const row of rows) {
        if (!row.controls.includes(control)) continue
        const keys = groups.get(row.shortTitle) || []
        keys.push(row.input)
        groups.set(row.shortTitle, keys)
    }
    return Array.from(groups, ([action, inputs]) => ({action, inputs}))
}
