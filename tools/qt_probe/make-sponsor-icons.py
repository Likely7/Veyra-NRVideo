# The sponsor page's brand icons: qml/Veyra/sponsor/*.svg from Simple Icons glyphs.
#
# usage: python -B tools/qt_probe/make-sponsor-icons.py [SIMPLE_ICONS_DIR]
#
# SIMPLE_ICONS_DIR is the npm package simple-icons 16.34.0 (CC0-1.0), unpacked so that
# icons/<slug>.svg exists; see THIRD_PARTY_NOTICES.md. Each glyph is a single 24x24
# path. The page shows them as app-style tiles in the brand colour (a white glyph on
# the colour, or the colour on a light tile), and two of them in their own colours:
# Douyin's note with its cyan and red offsets, and Ko-fi's cup as its official button
# draws it (white cup, dark outline, red heart, white sticker edge).
import pathlib, re, sys

root = pathlib.Path(__file__).resolve().parents[2]
icons = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else
                     "E:/项目/Veyra/deps/claude-sponsor-20261019/simple-icons-16.34.0") / "icons"
out = root / "qml/Veyra/sponsor"
out.mkdir(parents=True, exist_ok=True)


def glyph(slug):
    svg = (icons / f"{slug}.svg").read_text(encoding="utf-8")
    paths = re.findall(r'<path d="([^"]+)"', svg)
    if len(paths) != 1 or 'viewBox="0 0 24 24"' not in svg:
        sys.exit(f"{slug}: expected one path in a 24x24 viewBox")
    return paths[0]


TOKEN = re.compile(r"[MmZzLlHhVvCcSsQqTtAa]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?")
ARGS = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4, "Q": 4, "T": 2, "A": 7, "Z": 0}


def num(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def subpaths(d):
    """Split path data into subpaths that each start with an absolute M.

    A relative m continues from the previous subpath's end point, so a subpath cut
    out of the middle of a path needs its start made absolute to draw on its own.
    """
    tokens = TOKEN.findall(d)
    segments, i, command = [], 0, None
    while i < len(tokens):
        if tokens[i].isalpha():
            command = tokens[i]
            i += 1
            if command in "Zz":
                segments.append((command, []))
                continue
        n = ARGS[command.upper()]
        args = [float(t) for t in tokens[i:i + n]]
        if len(args) != n:
            sys.exit("truncated path data")
        i += n
        segments.append((command, args))
        if command in "Mm":   # further pairs after a moveto are line segments
            command = "L" if command == "M" else "l"
    out, x, y, sx, sy = [], 0.0, 0.0, 0.0, 0.0
    for command, args in segments:
        rel = command.islower()
        kind = command.upper()
        if kind == "M":
            x, y = (x + args[0], y + args[1]) if rel else (args[0], args[1])
            sx, sy = x, y
            out.append([f"M{num(x)} {num(y)}"])
            continue
        out[-1].append(command + " ".join(num(a) for a in args))
        if kind == "Z":
            x, y = sx, sy
        elif kind == "H":
            x = x + args[0] if rel else args[0]
        elif kind == "V":
            y = y + args[0] if rel else args[0]
        else:
            ex, ey = args[-2], args[-1]
            x, y = (x + ex, y + ey) if rel else (ex, ey)
    return ["".join(parts) for parts in out]


def tile(fill, body, edge=False):
    """A 48x48 rounded tile; `edge` outlines dark tiles so they read on dark cards."""
    stroke = ' stroke="#FFFFFF" stroke-opacity="0.16" stroke-width="1"' if edge else ""
    rect = (f'<rect x="0.5" y="0.5" width="47" height="47" rx="11.5" fill="{fill}"{stroke}/>' if edge
            else f'<rect width="48" height="48" rx="12" fill="{fill}"/>')
    return rect + body


def placed(size, body):
    """A 24x24 glyph drawn `size` units wide in the middle of the 48x48 tile."""
    offset = (48 - size) / 2
    return f'<g transform="translate({num(offset)} {num(offset)}) scale({num(size / 24)})">{body}</g>'


def write(name, body, view=48):
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{view}" height="{view}" '
           f'viewBox="0 0 {view} {view}">{body}</svg>\n')
    (out / f"{name}.svg").write_text(svg, encoding="utf-8")
    print("wrote", name)


bilibili, tiktok, x, github, youtube, kofi = (glyph(s) for s in ("bilibili", "tiktok", "x", "github", "youtube", "kofi"))
wechat, alipay = glyph("wechat"), glyph("alipay")

write("bilibili", tile("#00A1D6", placed(28, f'<path fill="#FFFFFF" d="{bilibili}"/>')))
# Douyin uses the same note as TikTok: white, with cyan up-left and red down-right.
write("douyin", tile("#111111", placed(26,
      f'<path fill="#25F4EE" transform="translate(-0.7 -0.7)" d="{tiktok}"/>'
      f'<path fill="#FE2C55" transform="translate(0.7 0.7)" d="{tiktok}"/>'
      f'<path fill="#FFFFFF" d="{tiktok}"/>'), edge=True))
write("x", tile("#000000", placed(22, f'<path fill="#FFFFFF" d="{x}"/>'), edge=True))
write("github", tile("#F3F3F5", placed(28, f'<path fill="#181717" d="{github}"/>')))
# The play triangle is a hole in the glyph: it shows the red tile through the white.
write("youtube", tile("#FF0000", placed(28, f'<path fill="#FFFFFF" d="{youtube}"/>')))
# Payment labels: the glyph alone in its colour, 24x24.
write("wechat", f'<path fill="#07C160" d="{wechat}"/>', view=24)
write("alipay", f'<path fill="#1677FF" d="{alipay}"/>', view=24)

# Ko-fi: outer silhouette, cup inside, handle centre, heart (in that order in the glyph).
outer, inside, handle, heart = subpaths(kofi)
outer += "" if outer.endswith(("z", "Z")) else "z"   # closed, so the white edge has no seam
write("kofi", placed(40,
      f'<path fill="#FFFFFF" stroke="#FFFFFF" stroke-width="3.2" stroke-linejoin="round" d="{outer}"/>'
      f'<path fill="#202020" d="{outer}"/>'
      f'<path fill="#FFFFFF" d="{inside}"/>'
      f'<path fill="#202020" d="{handle}"/>'
      f'<path fill="#FF5A16" d="{heart}"/>'))
