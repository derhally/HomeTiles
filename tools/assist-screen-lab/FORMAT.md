# Assist screen file format (format 1)

An Assist screen is one JSON file. It describes shapes whose properties are
small expressions of time, voice level, phase and size. The panel compiles the
file once, then evaluates it every 33 ms frame into a draw list. Nothing in a
file is code the panel runs: it can only draw lines, arcs, discs, rectangles,
polylines and text. A file over any limit is refused with a message naming the
problem and its line.

`screens/` holds one complete example per built-in style (atom, reactor,
dot_halo, voice_pills, sentinel, phosphor, light_disc, lens, iris).

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
  "layouts": {},
  "items": []
}
```

| Key | Meaning |
| --- | --- |
| `format` | `1`. Additions to the format raise the number; a panel refuses a file with a higher number than it reads, with a message naming both ("this screen needs format 2; this panel reads format 1"), so a gallery can offer each panel only what it reads. Until format 1 ships, it may still change. |
| `id` | `a-z`, `0-9`, `_`, up to 32. It names the screen in Settings and the Bridge select. |
| `name` | Shown name, up to 32 bytes. Settings shows a built-in style's own translated name for the nine built-in ids; `name` is for new ids, like a tile title the user gives. |
| `needs` | Panel features the screen needs (`assist_style_catalog.h`). `wide` means the screen is at least 1.2 times as wide as tall. `tear_free` means the board swaps framebuffers (the 7B in landscape). A panel without them does not offer the screen. A screen with its own square and tall layouts does not need `wide`. |
| `label_color` | Colour of the "Listening..." line under the screen. |
| `reach` | How far the screen reaches, which sets the base radius `r` (`assist_screen::baseRadius`). `half_r` is the half width in `r` (default 2). `side_r` and `side_px` give `side_r * r + side_px` for readouts with a fixed pixel width. `up_r` and `down_r` (0 = not limited) are how far it reaches above and below the centre. `r` is 17 % of the short side, shrunk so the reach plus an 8 px edge fits. `label_r` places the "Listening..." line below the centre, in `r` (default 2.1). |
| `colors` | Named colours, `"#RRGGBB"`, usable in expressions. |
| `vars` | Named values, in order (below). |
| `layouts` | Other reach and vars per screen shape (below). |
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
| `wide`, `square`, `tall` | 1 for the screen's shape, else 0 (below) |
| `pi`, `tau` | 3.14159..., 6.28318... |

Functions:

- `sin cos tan asin acos atan atan2 sqrt abs min max clamp clamp01 mix floor ceil round fract mod exp pow log sign`
- `step(edge, x)` and `smoothstep(a, b, x)`.
- `mod` is always positive for a positive divisor, like GLSL.
- `hash(i, salt)` gives a fixed fraction per index, 0..1 (the former C++ styles' own).
- `rand(lo, hi)` gives a whole number in lo..hi; `frand()` gives 0..1.
- `prev(var, k)` is a state var's value `k` steps of its history ago (below).

Each step counts toward the per-frame budget: `sin cos tan asin acos atan
atan2 exp pow log sqrt` count 8 (software routines on the panel), other
functions and `prev` 2, everything else 1.

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

A state var can keep a history: `"keep": n` (up to 8) keeps its last `n`
values, newest first, read with `prev(name, k)` (`k` = 0 is the value now,
`k` past `n` reads the oldest). By default every step adds the old value;
`"record": "expr"` adds it only at steps where the expression is not 0. Light
Disc keeps its trails' last corners this way:

```json
"p_cx": {"init": 0, "next": "p_turn ? p_nx : p_cx", "keep": 8, "record": "p_turn"}
```

Up to 16 vars may keep a history.

## Layouts

A screen is `wide` when it is at least 1.2 times as wide as tall, `tall` when
it is at least 1.2 times as tall as wide, and `square` otherwise (1024x600 and
800x480 are wide, 720x720 and 480x480 square, 480x800 and 720x1280 tall).

Readouts with a fixed pixel size that sit beside the rings on a wide screen
collide with them on a narrow one. A layout moves them instead:

```json
"layouts": {
  "tall": {"reach": {"half_r": 2.0, "side_r": 0, "side_px": 0},
           "vars": {"by": "cy - 2.55 * r", "hy": "cy + 3.3 * r"}},
  "square": {"vars": {"bx": "16", "by": "16 + 0.38 * r"}}
}
```

- `reach` keys replace the file's for that shape, so `r` can stay large when
  the readouts no longer sit beside the rings.
- `vars` give derived vars another formula for that shape. Only derived vars
  can change (not state vars), and each must be in the file's `vars`. Put every
  position a layout may move in a var, as `screens/sentinel.json` does.
- A shape without a layout uses the file as written.
- Elements for one shape only take `"on": ["wide"]`; formulas can also read
  `wide`, `square` and `tall`.

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
| `on` | The shapes it is drawn on, for example `["wide"]`; default all. On a group it applies to everything inside. |

Shapes (unlisted properties take their defaults; `color` defaults to white,
`alpha` to 1, `width` to 1):

| Type | Properties |
| --- | --- |
| `line` | `x1 y1 x2 y2 width color alpha`; `dash gap` in px for a dashed line |
| `arc` | `x y radius start end width color alpha`; degrees, 0 = right, clockwise; `radius` is the outer edge; a 360° span is a ring |
| `disc` | `x y radius color alpha`; `grad` shades it from `color` at the top to `grad` at the bottom |
| `rect` | `x1 y1 x2 y2 color alpha radius` (filled) |
| `outline` | `x1 y1 x2 y2 width color alpha` (an outlined box, square corners) |
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

Keys: `x y` (top left), `box_width`, `font`, `align`
(`left`/`center`/`right`), `color`, `alpha` and `frames`. `frames` refreshes the
text only every n frames (like a state var's `frames`); the position is
updated every frame.

- `font` is 16, 20, 24, 28, 32, 40 or 48: the sizes every panel has.
- `text` is a template: `"PK {peak * 100:03d}"`. Inside `{expr:fmt}`, `fmt` is
  a printf conversion without `%` (`d`, `x`, `X`, `f` with flags, and a width
  and precision of at most two digits each; for example `02X` or `06.1f`).
- The text can hold up to 47 bytes. Each fixed part between `{...}` is at most
  8 bytes: files carry readout glyphs and units (`PK`, `CH1`, `V`, `T+`), never
  words for people. Words belong in the firmware's central translations, like
  the "Listening..." line the panel draws itself.

### log

Rows that scroll up as new ones arrive. Row `k` (0 at the top) is drawn at
`y + k * line_height`; `color` and `alpha` may use `k`.

| Key | Meaning |
| --- | --- |
| `line` | Template of a new row, like `text` |
| `rows` | Rows kept, up to 8 |
| `frames` | Frames between steps (an expression) |
| `push` | When non-zero at a step, a new row is added and the oldest drops when full |
| `pop` | When non-zero at a step (and `push` is 0), the oldest row is removed |
| `fill` | Start full |
| `id` | Gives the var `<id>_count` (rows shown), usable anywhere in the file, for example to show a plate under the rows only when there are any |

## Panel limits

These are checked when the file loads, except the fill budget, which is
checked as each frame is built. `src/ui/assist/assist_screen.h` holds the
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
| Expression steps per frame | ≤ 60000 (weighted, above), worst case | CPU time per 33 ms frame |
| Fill per frame | ≤ 6 times the screen area, estimated per shape (glows included); radii are clamped to the screen diagonal | Shapes past the budget are left out (counted, logged once) |
| History | 16 vars, 8 values each | Fixed array |
| Fixed text | 8 bytes per part; number formats two digits | Readouts only |
| Memory | One fixed block of about 108 KB (PSRAM on the panel), allocated once | The engine allocates nothing per load or frame |

A loaded file costs at most these limits, however its expressions behave. The
LVGL draw calls allocate their draw tasks like any widget does; the engine
issues only the commands that reach the band LVGL is drawing.

Redraw: each top-level element keeps the box it drew last frame and this frame,
and only the elements whose drawing changed are redrawn. Overlapping boxes are
joined, and the panel gets at most 8 areas. The first 24 top-level elements
are regions of their own; the rest share the last one. A frame with no visible
change redraws nothing. Put parts that move independently (side readouts, a moving
box) in their own top-level elements, not in one group, so they redraw on
their own.
