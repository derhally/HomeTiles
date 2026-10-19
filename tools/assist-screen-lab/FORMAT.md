# Assist screen file format (format 1)

An Assist screen is one JSON file. It describes shapes whose properties are
small expressions of time, voice level, phase and size. The panel compiles the
file once, then evaluates it every 33 ms frame into a draw list. Nothing in a
file is code the panel runs: it can only draw lines, arcs, discs, rectangles,
polylines and text. A file over any limit is refused with a message naming the
problem and its line.

`screens/sentinel.json`, `screens/iris.json` and `screens/phosphor.json` are
complete examples (ports of the built-in styles).

## Top level

```json
{
  "format": 1,
  "id": "sentinel",
  "name": "Sentinel",
  "note": "free text",
  "needs": ["wide"],
  "label_color": "#E8FBFF",
  "reach": {"half_r": 3.95, "side_r": 3.27, "side_px": 110},
  "colors": {"cyan": "#5CE1FF"},
  "vars": {},
  "items": []
}
```

| Key | Meaning |
| --- | --- |
| `format` | Always `1`. |
| `id` | `a-z`, `0-9`, `_`, up to 32. It names the screen in Settings and the Bridge select. |
| `name` | Shown name, up to 32 bytes. |
| `needs` | Panel features the screen needs (`assist_style_catalog.h`). `wide` means the screen is at least 1.2 times as wide as tall. `tear_free` means the board swaps framebuffers (the 7B in landscape). A panel without them does not offer the screen. |
| `label_color` | Colour of the "Listening..." line under the screen. |
| `reach` | How far the screen reaches sideways, which sets the base radius `r` (as `assist_styles::baseRadius` does). `half_r` is the half width in `r` (default 2). `side_r` and `side_px` give `side_r * r + side_px` for readouts with a fixed pixel width. `r` is 17 % of the short side, shrunk so the reach plus an 8 px edge fits. |
| `colors` | Named colours, `"#RRGGBB"`, usable in expressions. |
| `vars` | Named values, in order (below). |
| `items` | The shapes, drawn in order (later ones on top). |
| `note` | Free text; also allowed on every element. |

## Expressions

Numbers, names, `+ - * / ( )`, comparisons `< <= > >= == !=`, `&& || !`,
and `cond ? a : b`. A colour is a number (`#5CE1FF` or a colour name), so
`k >= 8 ? amber : cyan` works.

These names are provided:

| Name | Value |
| --- | --- |
| `t` | Seconds since the screen opened |
| `dt` | Seconds since the last frame |
| `frame` | Frame number |
| `level` | Voice (listening) or answer (answering) loudness, 0..1 |
| `mode` | 0 for the listening look up to 1 for the thinking/answering look, eased |
| `answering` | 1 while the answer plays, else 0 |
| `r` | Base radius; it pops in from 0 as the screen opens |
| `cx`, `cy` | Centre |
| `w`, `h` | Screen size |
| `pi`, `tau` | 3.14159..., 6.28318... |

Functions:

- `sin cos tan asin acos atan atan2 sqrt abs min max clamp clamp01 mix floor ceil round fract mod exp pow log sign`
- `step(edge, x)` and `smoothstep(a, b, x)`.
- `mod` is always positive for a positive divisor, like GLSL.
- `hash(i, salt)` gives the built-in styles' fixed fraction per index, 0..1.
- `rand(lo, hi)` gives a whole number in lo..hi; `frand()` gives 0..1.

NaN and infinity become 0. Coordinates are clamped to ±30000 px and snapped to
half pixels, so movement too small to see does not redraw.

## Vars

Vars are evaluated in order each frame, so a var can use the ones above it.

- `"name": "expr"` is derived, recomputed every frame.
- `"name": {"init": "expr", "next": "expr"}` is state: `init` at the start,
  then `next` (which may use the var itself) every frame.
- `"every": seconds` or `"frames": n` updates state less often. Without `next`
  the var keeps its `init`, for example a random seed.

```json
"answer": {"init": 0, "next": "answer + (answering - answer) * 0.12"},
"think": "max(0, mode - answer)",
"box_an": {"init": 0, "next": "rand(0, 628) / 100", "every": 0.7}
```

## Elements

Every element has `type` and may have:

| Key | Meaning |
| --- | --- |
| `repeat` | Draw it n times (a constant, up to 256). |
| `as` | Name of the repeat index (default `i`). |
| `let` | Named values per repeat, in order: `{"ang": "i * tau / 72", "c": "cos(ang)"}`. |
| `if` | Skip this repeat (or the whole group) when 0. |
| `glow` | Also draw a soft copy, 4 times as wide at 16 % alpha. |
| `round` | Round line ends. Defaults to true for line, arc and poly. |

Shapes (unlisted properties take their defaults; `color` defaults to white,
`alpha` to 1, `width` to 1):

| Type | Properties |
| --- | --- |
| `line` | `x1 y1 x2 y2 width color alpha`; `dash gap` in px for a dashed line |
| `arc` | `x y radius start end width color alpha`; degrees, 0 = right, clockwise; `radius` is the outer edge; a 360° span is a ring |
| `disc` | `x y radius color alpha` |
| `rect` | `x1 y1 x2 y2 color alpha radius` (filled) |
| `frame` | `x1 y1 x2 y2 width color alpha` (outline, square corners) |
| `poly` | A polyline, below |
| `text` | A text line, below |
| `log` | Rows of text that scroll, below |
| `group` | `items` drawn together under one `if`/`repeat`; up to 4 deep |

### poly

Use either `"points": [["x", "y"], ...]` (up to 16 points) or `"count": n`
(up to 128) with `x` and `y` expressions of `i` (the point index) and
`u = i / (count - 1)`. `let` runs once before the points. `trail` keeps older
copies that fade behind it:

```json
"trail": {"keep": 3, "frames": 3, "width": 2, "alpha": [0.30, 0.18, 0.10]}
```

Here the current points join the trail every 3 frames, the 3 newest copies are
kept, and the newest copy comes first in `alpha`. Up to 4 copies.

### text

Keys: `x y` (top left), `width` (box width), `font`, `align`
(`left`/`center`/`right`), `color`, `alpha` and `every`. `every` refreshes the
text only every n frames; the position is updated every frame.

- `font` is 16, 20, 24, 28, 32, 40 or 48: the sizes every panel has.
- `text` is a template: `"PK {peak * 100:03d}"`. Inside `{expr:fmt}`, `fmt` is
  a printf conversion without `%` (`d`, `x`, `X`, `f` with flags, width and
  precision; for example `02X` or `06.1f`).
- The text can hold up to 47 bytes.

### log

Rows that scroll up as new ones arrive. Row `k` (0 at the top) is drawn at
`y + k * line_height`; `color` and `alpha` may use `k`.

| Key | Meaning |
| --- | --- |
| `line` | Template of a new row, like `text` |
| `rows` | Rows kept, up to 8 |
| `every` | Frames between steps (an expression) |
| `push` | When non-zero at a step, a new row is added and the oldest drops when full |
| `pop` | When non-zero at a step (and `push` is 0), the oldest row is removed |
| `fill` | Start full |
| `id` | Gives the var `<id>_count` (rows shown), usable anywhere in the file, for example to show a plate under the rows only when there are any |

## Panel limits

These are checked when the file loads. `engine/assist_screen.h` holds the
constants.

| Limit | Value | Why |
| --- | --- | --- |
| File size | 24 KB | Download and parse in one bounded buffer |
| JSON values | 2048, nesting 12 | Fixed parser arena |
| Expression code | 4096 steps in total, nesting 24 | Fixed compiled code array |
| Names | 192 (vars, lets, indexes); 64 vars, 160 lets, 24 colours | Fixed slot table |
| Elements | 160, groups 4 deep, repeat ≤ 256 | Fixed element table |
| Polylines | 8, ≤ 128 points, trail ≤ 4; 2048 points in total | Fixed point pool |
| Texts | 24 buffers (texts plus log rows) of 48 bytes; 4 logs | Fixed text buffers, no allocation per change |
| Shapes per frame | ≤ 1200, worst case over every branch and repeat | Draw time on the panel |
| Expression steps per frame | ≤ 60000, worst case | CPU time per 33 ms frame |
| Memory | One fixed block of about 145 KB (PSRAM on the panel), allocated once | Nothing is allocated per load or frame |

A loaded file costs at most these limits, however its expressions behave.

Redraw: each top-level element keeps the box it drew last frame and this frame,
and only the elements whose drawing changed are redrawn. Overlapping boxes are
joined, and the panel gets at most 8 areas. A frame with no visible change
redraws nothing. Put parts that move independently (side readouts, a moving
box) in their own top-level elements, not in one group, so they redraw on
their own.
