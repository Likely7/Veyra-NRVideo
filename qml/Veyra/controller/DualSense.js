// SPDX-License-Identifier: AGPL-3.0-only
// Front elevation adapted from Stickmouse at cc2e75ff. See controller/LICENSE.
// Qt's SVG image loader draws the same contours in both idle and highlighted states.
.pragma library

// Keep a generous pointer target for the small keys. The two sticks need a
// circular hit test so a press on either side of the rim selects stick travel.
function contains(id, port, x, y) {
    if (/^[lr][12]$/.test(id)) {
        const shoulderX = id[0] === "l" ? x : 270 - x
        if (shoulderX < 35 || shoulderX > 72) return false
        // Follow the curved seam between the two narrow shoulder switches.
        const t = Math.max(0, Math.min(1, (shoulderX - 36.8) / 33.3))
        const seam = 16.5 - 17.7 * t + 10.8 * t * t - .6 * t * t * t
        return id[1] === "2" ? y >= seam - 6 && y < seam : y >= seam && y <= seam + 4.5
    }
    if (id.startsWith("dpad_")) {
        const dx = x - 53.5, dy = y - 55.5
        const vertical = Math.abs(dy) >= Math.abs(dx)
        const distance = Math.max(Math.abs(dx), Math.abs(dy))
        const side = vertical ? (dy < 0 ? "up" : "down") : (dx < 0 ? "left" : "right")
        return id === "dpad_" + side && distance >= 2 && distance <= 21.5
               && Math.min(Math.abs(dx), Math.abs(dy)) <= 9.5
    }
    if (id === "l3" || id === "r3" || id === "left_stick" || id === "right_stick") {
        const left = id === "l3" || id === "left_stick"
        const radius = Math.hypot(x - (left ? 93.2 : 176.8), y - 90.8)
        return id === "l3" || id === "r3" ? radius <= 12.3 : radius > 12.3 && radius <= 21.6
    }
    if (id === "touchpad") {
        const dx = x - 135
        const halfWidth = 53 - Math.max(0, y - 20) * .21 - Math.max(0, y - 56) * .7
        return y >= 6.5 + .00155 * dx * dx && y <= 65 && Math.abs(dx) <= halfWidth
    }
    return Math.abs(x - port[0]) <= port[2] / 2 && Math.abs(y - port[1]) <= port[3] / 2
}

function source(selected, hovered, focused, accent, ink) {
    function tint(id, shape) {
        if (id !== selected && id !== hovered && id !== focused) return ""
        const stroke = id === focused ? 1.5 : id === selected ? 1.1 : .75
        const fillOpacity = id === selected ? .55 : id === focused ? .18 : .30
        return `<use xlink:href="#${shape}" fill="none" stroke="${ink}" stroke-opacity=".82" stroke-width="${stroke + .75}"/>
                <use xlink:href="#${shape}" fill="${accent}" fill-opacity="${fillOpacity}" stroke="${accent}" stroke-width="${stroke}"/>`
    }

    function shoulder(side) {
        return `<use xlink:href="#trigger" fill="#242833" stroke="#10121a" stroke-width="1.1"/>
                <path d="M39.3 14.8c8.3-5.5 18.4-8.4 28.1-8.5" fill="none" stroke="#4b505d" stroke-width="1"/>
                ${tint(side + "2", "trigger")}
                <use xlink:href="#bumper" fill="#161923"/>${tint(side + "1", "bumper")}`
    }

    function menuKey(id) {
        return `<use xlink:href="#menuKey" fill="url(#key)" stroke="#8b929f" stroke-width=".85"/>${tint(id, "menuKey")}`
    }

    function direction(id, angle) {
        return `<g transform="rotate(${angle} 53.5 55.5)">
                  <use xlink:href="#direction" fill="url(#key)" stroke="#79808e" stroke-width="1.05"/>${tint(id, "direction")}
                  <path d="m53.5 39-2 2.4h4z" fill="#717784"/>
                </g>`
    }

    function face(id, x, y) {
        return `<g transform="translate(${x} ${y})">
                  <use xlink:href="#faceCap" fill="#adb3c0" stroke="#777e8d" stroke-width=".8"/>
                  <circle cy="-.35" r="7.95" fill="url(#key)" stroke="#f5f7fb" stroke-width=".65"/>
                  ${tint(id, "faceCap")}
                </g>`
    }

    function stick(travel, press, x) {
        return `<g transform="translate(${x} 90.8)">
                  <circle r="21.1" fill="url(#socket)"/>
                  <circle r="15.8" fill="url(#cap)" stroke="#10131b" stroke-width="1.35"/>
                  <use xlink:href="#stickCenter" fill="none" stroke="#474c59" stroke-opacity=".6" stroke-width=".65"/>
                  ${tint(travel, "stickRim")}${tint(press, "stickCenter")}
                </g>`
    }

    const svg = `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"
     width="270" height="184" viewBox="0 0 270 184" fill="none"
     stroke-linejoin="round" stroke-linecap="round">
  <defs>
    <linearGradient id="shell" x2=".32" y2="1" gradientUnits="objectBoundingBox">
      <stop stop-color="#fafbff"/><stop offset=".56" stop-color="#e4e7ef"/><stop offset="1" stop-color="#c1c5d0"/>
    </linearGradient>
    <linearGradient id="panel" x2="0" y2="1">
      <stop stop-color="#131723"/><stop offset=".6" stop-color="#262a37"/><stop offset="1" stop-color="#171a24"/>
    </linearGradient>
    <linearGradient id="touch" x2="0" y2="1">
      <stop stop-color="#f2f4fa"/><stop offset=".22" stop-color="#e6e9f0"/><stop offset="1" stop-color="#d6dae4"/>
    </linearGradient>
    <linearGradient id="key" x2=".4" y2="1">
      <stop stop-color="#fafbff"/><stop offset=".52" stop-color="#e1e5ed"/><stop offset="1" stop-color="#b9becb"/>
    </linearGradient>
    <radialGradient id="socket" cx=".47" cy=".43" r=".58">
      <stop offset=".67" stop-color="#0e1017"/><stop offset=".83" stop-color="#121520"/>
      <stop offset=".95" stop-color="#303440"/><stop offset="1" stop-color="#1a1d27"/>
    </radialGradient>
    <radialGradient id="cap" cx=".44" cy=".35" r=".7">
      <stop stop-color="#2e323e"/><stop offset=".56" stop-color="#252935"/>
      <stop offset=".77" stop-color="#353945"/><stop offset=".88" stop-color="#282b36"/><stop offset="1" stop-color="#171a22"/>
    </radialGradient>
    <path id="trigger" d="M36.8 13.9C44 8.3 56.9 3.8 67.6 5.2l2.9.7.4 3.2c-11.1-.9-23.5 1.5-34.1 7.4z"/>
    <path id="bumper" d="M36.8 16.5c10.6-5.9 22.2-8.2 33.3-7.5l.6 4.5L35.5 21z"/>
    <path id="grip" d="M82.7 11.8C64.4 10.3 47.9 13.4 32.4 19.9 20.2 40.3 10 72 5.8 105.5 2.4 131.7 3.2 158.2 10 170.2c3.2 5.6 7.4 8 13.5 7.7 2.5-.3 3.9-2.5 5.2-6.8 8.2-27.7 18.3-55.3 32.1-81 5.5-10.2 10.4-17.6 16-24.2 5.4-6.4 7.2-10.5 7.6-16.3z"/>
    <path id="direction" d="M46.7 36.3q6.6-3.3 13.3 0c2.2 1.1 2.4 4.4 1.5 9.9L55.4 52q-1.8 1.6-3.6 0l-5.7-5.9c-.9-4.8-1.3-8.3.6-9.8z"/>
    <circle id="faceCap" r="8.9"/>
    <rect id="menuKey" x="-3.2" y="-6" width="6.4" height="11.9" rx="3"/>
    <path id="stickRim" fill-rule="evenodd" d="M-21.1 0a21.1 21.1 0 1 0 42.2 0 21.1 21.1 0 1 0-42.2 0z M-12.3 0a12.3 12.3 0 1 1 24.6 0 12.3 12.3 0 1 1-24.6 0z"/>
    <circle id="stickCenter" r="12.3"/>
    <path id="touchplate" d="M84.9 11.1C96.5 7.6 116.3 7 135 7s38.5.6 50.1 4.1c2.9.9 3.4 4.6 2.5 9.5l-6.6 32.1c-1.6 8-5.7 11.8-13.4 12h-65.2c-7.7-.2-11.8-4-13.4-12l-6.6-32.1c-.9-4.9-.4-8.6 2.5-9.5z"/>
    <path id="home" d="M132.8 81.6c4.2.8 8.6 2 8.6 5.2 0 2.2-1.1 3.6-3.2 3.2v-4.7c0-1.1-.6-1.6-1.5-1.8v10.6l-3.9-1.2z M131.4 88.6c-4.2 1-6.3 2.2-5.8 3.2.4.8 2.9 1.3 6 1.3v-1.8c-2.6.2-3.2-.6-.5-1.3l.3-.1zm6 2.4c1.9-.8 4.4-1 6.4-.5s1.9 1.6.1 2.5c-1.7.8-4.2 1.6-6.5 2v-1.6l2.9-.8c1.3-.5 1.4-1 .1-1.1-1.1 0-2 .2-3 .6z"/>
  </defs>

  ${shoulder("l")}
  <g transform="translate(270 0) scale(-1 1)">${shoulder("r")}</g>
  <!-- The dark chassis continues down both inside grip edges. -->
  <path d="M135 11.5c-38 0-76-4.2-102.6 8.4C19 44.1 7.8 83.8 4.8 117.6 2.2 147.8 5.8 173.8 19.9 177.4l18.8 2c6-17.4 12.2-35.2 19.9-49 4.4-7.9 11-12.6 19.7-12.5l13 1.2h87.4l13-1.2c8.7-.1 15.3 4.6 19.7 12.5 7.7 13.8 13.9 31.6 19.9 49l18.8-2c14.1-3.6 17.7-29.6 15.1-59.8-3-33.8-14.2-73.5-27.6-97.7C211 7.3 173 11.5 135 11.5z"
        fill="url(#panel)" stroke="#454a56" stroke-width="1.2"/>
  <g fill="url(#shell)" stroke="#b8bdc9" stroke-width="1">
    <use xlink:href="#grip"/><use xlink:href="#grip" transform="translate(270 0) scale(-1 1)"/>
  </g>
  <path d="M80.9 14.8q-.9 2.7.1 8.1l6.5 32.2q2.3 11.3 15.1 11.7h64.8q12.8-.4 15.1-11.7l6.5-32.2q1-5.4.1-8.1"
        fill="#171d29" stroke="#202636" stroke-width="1.25"/>
  <path d="M81.6 13.8c-1 2.4-1.3 5-.7 7.9l6.1 28.5m101.4-36.4c1 2.4 1.3 5 .7 7.9L183 50.2"
        stroke="#467eff" stroke-width="2.1"/>
  <use xlink:href="#touchplate" fill="url(#touch)" stroke="#b8becb" stroke-width=".85"/>
  ${tint("touchpad", "touchplate")}
  <g transform="translate(74.1 26.2) rotate(-14)">${menuKey("share")}</g>
  <g transform="translate(195.9 26.2) rotate(14)">${menuKey("options")}</g>
  <g stroke="#89909e" stroke-width=".72">
    <path d="m71 17-1-1.8m3.1 1.1-.2-2m2.3 2 .7-1.9m119.2 1.4 3.7.8m-4.1.7 3.8.8"/>
  </g>
  ${direction("dpad_up", 0)}
  ${direction("dpad_right", 90)}
  ${direction("dpad_down", 180)}
  ${direction("dpad_left", 270)}
  ${face("triangle", 216.5, 35.8)} ${face("square", 197.4, 55.3)}
  ${face("circle", 235.7, 55.3)} ${face("cross", 216.5, 74.5)}
  <g stroke="#7b8290" stroke-width="1.12">
    <path d="m216.5 30.9-4.7 8.2h9.4zM193.6 51.5h7.6v7.6h-7.6zM212.8 70.8l7.4 7.4m0-7.4-7.4 7.4"/>
    <circle cx="235.7" cy="55.3" r="4.8"/>
  </g>
  ${stick("left_stick", "l3", 93.2)} ${stick("right_stick", "r3", 176.8)}
  <g fill="#090c13" stroke="#3e4350" stroke-width=".3">
    <circle cx="125.9" cy="71" r="1.35"/><circle cx="130.4" cy="71" r="1.35"/>
    <circle cx="135" cy="71" r="1.35"/><circle cx="139.6" cy="71" r="1.35"/><circle cx="144.1" cy="71" r="1.35"/>
    <circle cx="128.1" cy="75.5" r="1.15"/><circle cx="132.7" cy="75.5" r="1.15"/>
    <circle cx="137.3" cy="75.5" r="1.15"/><circle cx="141.9" cy="75.5" r="1.15"/>
  </g>
  <!-- Raised PS home button, microphone mute bar and indicator. -->
  <g fill="#3c414c" stroke="#0c0f16" stroke-width=".8">
    <use xlink:href="#home"/>
    ${tint("ps", "home")}
    <rect x="128.8" y="102.7" width="12.4" height="2.7" rx="1.35"/>
  </g>
  <circle cx="135" cy="110.7" r=".7" fill="#545966"/>
</svg>`
    return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg)
}
