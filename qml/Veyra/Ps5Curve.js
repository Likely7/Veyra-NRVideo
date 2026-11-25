// SPDX-License-Identifier: AGPL-3.0-only
// Adapted from Stickmouse Mapper, crates/stickmouse-mapper/src/mouse_aim/curve_fit.rs
// at 826547879659100e5e3b3e4c975f205d15cf66fb. Return degrees/s for the graph.
.pragma library

function samples(axis, fit) {
    if (!axis || (fit !== undefined && typeof fit !== "boolean")) return []
    const points = axis.points
    const knots = [[axis.deadzone, 0]].concat(points)
    const result = [[0, 0]]
    if (Math.fround(axis.deadzone) > 0) result.push(knots[0])
    if (fit === false) return result.concat(points)

    const widths = [], slopes = []
    for (let i = 0; i < points.length; ++i) {
        widths.push(points[i][0] - knots[i][0])
        slopes.push((points[i][1] - knots[i][1]) / widths[i])
    }
    const tangents = [slopes[0]]
    for (let i = 1; i < points.length; ++i) {
        const left = widths[i - 1], right = widths[i]
        const before = slopes[i - 1], after = slopes[i]
        tangents.push(before === 0 || after === 0 ? 0 : 3 * (left + right)
                      / ((left + 2 * right) / before + (2 * left + right) / after))
    }
    tangents.push(slopes[slopes.length - 1])
    let power = 0
    if (points.length >= 3) {
        const slow = points[0][1], fast = points[2][1]
        const scale = (points[2][0] - axis.deadzone) / widths[0]
        const exponent = Math.log(points[1][1] / slow)
                       / Math.log((points[1][0] - axis.deadzone) / widths[0])
        if (exponent > 1 && Math.abs(slow * scale / fast - 1) > 0.1
                         && Math.abs(slow * Math.pow(scale, exponent) / fast - 1) <= 0.1) {
            power = exponent
            tangents[1] = power * slow / widths[0]
        }
    }
    const spare = 64 - result.length - points.length
    const fullSpeed = points[points.length - 1][1]
    let allocated = 0
    for (let i = 0; i < points.length; ++i) {
        const start = knots[i][0], low = knots[i][1]
        const end = points[i][0], high = points[i][1]
        const total = Math.round(spare * (end - axis.deadzone) / (1 - axis.deadzone))
        const steps = 1 + total - allocated
        allocated = total
        for (let j = 1; j < steps; ++j) {
            let t = j / steps
            let speed
            if (i === 0 && power) {
                t *= t
                speed = high * Math.pow(t, power)
            } else {
                const u = 1 - t
                speed = low + t * (t * (3 - 2 * t) * (high - low)
                      + widths[i] * u * (u * tangents[i] - t * tangents[i + 1]))
            }
            const stick = Math.fround(start + widths[i] * t)
            if (stick > result[result.length - 1][0] && stick < Math.fround(end))
                result.push([stick, Math.fround(Math.max(low, Math.min(high, speed)) / fullSpeed) * fullSpeed])
        }
        result.push([Math.fround(end), Math.fround(high / fullSpeed) * fullSpeed])
    }
    return result
}
