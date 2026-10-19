# Assist screens from data files

Design reference for Assist screens described by data instead of compiled
into the firmware, and for the Assist Screen Lab that develops and checks
them on a desktop. Status: prototype. The engine is in the firmware tree but
compiled only into test builds (`HOMETILES_ASSIST_DATA_SCREENS=1`); release
builds are unchanged. Not yet measured on panel hardware.

| Part | Where |
| --- | --- |
| File format reference | `tools/assist-screen-lab/FORMAT.md` |
| Engine (compile, frame, redraw, draw) | `src/ui/assist/assist_screen.*` |
| Screen files, one per built-in style | `tools/assist-screen-lab/screens/*.json` |
| Built-in copies for the firmware (generated) | `src/ui/assist/assist_screen_files.h`, `tools/generate-assist-screens.mjs` |
| Overlay integration (test builds) | `src/ui/assist/assist_overlay.cpp` |
| Desktop lab (window and headless) | `tools/assist-screen-lab/lab.cpp`, `build.mjs`, `README.md` |
| Host tests | `tools/tests/assist/test-assist-screen-files.mjs`, `test-assist-screen-render.mjs` |
| Built-in styles it reproduces | `src/ui/assist/assist_styles*.cpp`, `assist_style_catalog.h` |

## Why

The Assist overlay offers nine styles (Atom, Reactor, Dot Halo, Voice Pills,
Sentinel, Phosphor, Light Disc, Lens, Iris), each hand-written C++ against
LVGL. A new style or a tweak needs a firmware build and an OTA update. A data
file can instead be added, shared and changed without firmware.

The goal is files that look like the built-in styles, cost about the same on
the panel, and cannot hang or crash the panel however they are written,
because a file may come from a download. The first two are shown on the
desktop; the panel cost still needs measuring (below).

## How a screen runs

A file is a JSON object with colours, named values (vars), optional layouts
per screen shape, and a list of shapes (items) whose properties are small
formulas. The engine works in three steps.

1. **Load (once per showing).** A bounded JSON parser builds a fixed DOM over
   the file text. Every formula is compiled (Pratt parser) to a small
   stack-machine program in one fixed code array. Names resolve to slots:
   built-in inputs, vars, per-element lets and repeat indexes; a name means
   one thing (a var, a let or a colour, never two). The engine counts the
   worst case per frame over every branch and repeat (repeat counts are
   constants), weighing transcendental functions more than arithmetic, and
   refuses the file if it is over any limit, with a message naming the
   problem and its line.
2. **Frame (every 33 ms).** The host sets the inputs, the engine steps the
   vars in order (a state var may keep a short history, read with `prev()`),
   then runs each element's formulas for each repeat into a flat draw list:
   lines (also dashed), arcs and rings, discs (optionally with a vertical
   gradient), filled and outlined rectangles, polylines with fading trail
   copies, text and scrolling text rows. Each shape is charged its estimated
   painted area; past the budget shapes are left out and counted.
   Coordinates are clamped and snapped to half pixels, so movement too small
   to see draws the same list.
3. **Redraw and draw.** Each top-level element keeps a hash of its commands
   and the box it covered this frame and the frame before. Only the boxes of
   elements whose drawing changed are invalidated (joined, at most 8 areas); a
   frame with no visible change invalidates nothing. LVGL renders the screen
   in bands and calls the painter once per band; `draw()` skips regions and
   commands whose box misses the band and replays the rest with
   `lv_draw_line/arc/rect/label`, the calls the line styles already use.

The inputs are what the overlay already feeds the built-in styles
(`assist_styles::Frame`):

| Name | Value |
| --- | --- |
| `t`, `dt`, `frame` | Seconds since the screen opened, seconds since the last frame, frame number |
| `level` | Voice (listening) or answer (answering) loudness, 0..1, eased by the overlay |
| `mode` | 0 for the listening look up to 1 for the thinking/answering look, eased |
| `answering` | 1 while the answer plays |
| `r` | Base radius; it pops in from 0 as the screen opens |
| `cx`, `cy`, `w`, `h` | Centre and screen size |
| `wide`, `square`, `tall` | 1 for the screen's shape |

Animation is maths, not keyframes: `sin(t * 2)` pulses, `x + (target - x) *
0.12` eases, a state var with `every: 0.7` and `rand()` jumps every 0.7 s, and
`mode` cross-fades the listening and thinking looks. State vars (`init`,
`next`) carry anything that must remember; `keep` and `prev(var, k)` give a
short list (Light Disc's trail corners).

## Safety: why a file cannot hang or crash the panel

Every structural limit is checked at load (`assist_screen.h`, table in
`FORMAT.md`): file size 24 KB, JSON values and nesting, compiled code 4096
steps, 192 names, 160 elements, repeat 256, groups 4 deep, polylines and their
point pool, 16 history vars, 24 text buffers of 48 bytes, at most 1,200 shapes
and 60,000 weighted formula steps per frame in the worst case.

A shape count alone does not bound drawing: one disc can cover the screen. So
every frame also has a fill budget of 6 screens' worth of painted pixels,
estimated per shape from its size clipped to the screen (a line's length times
its width, a disc's area, an arc's length times its width, glows included);
radii are clamped to the screen diagonal. A test file of 1,024 full-screen
translucent discs took 376 ms per frame on a desktop before the budget; now a
frame stops after six of them.

One `Screen` is a fixed block of about 108 KB (PSRAM on the panel; the JSON
DOM, needed only while loading, shares memory with the per-frame draw list),
allocated once. The engine allocates nothing per load or frame; the LVGL draw
calls allocate their draw tasks like any widget's. NaN and infinity become 0,
formula values become integers only through a clamping conversion, and box
edges are clamped before they become pixels. A file can only describe shapes:
no loops of variable length, no user functions, no access to Home Assistant
data, touch or storage, only the font sizes every panel carries. Unknown keys,
unknown shapes, name clashes and layouts that change state vars are refused
instead of guessed.

The host test runs a few thousand mutated copies of the nine files (truncated,
bytes flipped, tokens inserted, numbers swapped for extremes) through frames
at three sizes under AddressSanitizer and UndefinedBehaviorSanitizer. It found
two integer overflows, now fixed.

## Words in files

Files carry readout glyphs and units, never words for people: each fixed text
part is at most 8 bytes (`PK`, `CH1`, `V`, `T+`). The "Listening..." line is
drawn by the overlay from the central translations (`tr().assist_*`), and
Settings shows the built-in styles' translated names for the nine built-in
ids. A file's `name` is only for a new id, like a tile title.

## Format versions

`format` is 1. Additions raise the number; a panel refuses a file with a
higher number than it reads, with a message naming both, so a gallery can
offer each panel only what it reads. Unknown keys are refused (safety), so a
new key always comes with a new format number. Until format 1 ships it may
still change: the review renamed `frame` to `outline` and the text keys to
`box_width` and `frames` while nothing depended on them.

## Screen shapes and layouts

Readouts with a fixed pixel size beside the rings fit a wide screen and
collide with the rings on a narrow one. The engine classifies the screen like
`assist_style_catalog` does (`kWideAspect` 1.2): `wide` (at least 1.2 times as
wide as tall: 1024x600, 800x480, 1280x800), `tall` (480x800, 720x1280) or
`square` (720x720, 480x480).

A file may add `"layouts": {"tall": {...}, "square": {...}}`. Per shape it can
replace `reach` keys (how far the screen reaches sideways, above and below,
which sets `r`, and where the label sits) and give derived vars another
formula; elements can be limited to shapes with `"on": ["wide"]`. Positions a
layout may move therefore live in vars. `assist_screen::baseRadius(reach, w,
h)` is the one radius formula; the built-in styles call it with their own
reach.

Sentinel keeps its side readouts on wide screens, puts them above and below
the rings on tall ones and in the top corners on square ones; Phosphor narrows
its scope on tall and square screens instead of shrinking. Both therefore drop
the built-in styles' `needs: wide`.

## The screen files

All nine built-in styles are files. The render test compares each with its
built-in style at 1024x600, 800x480, 480x480 and 720x1280 (Sentinel and
Phosphor on wide sizes only): at most 6 % of the pixels differ, from the
random parts. Worst-case cost per frame:

| File | Needs | Layouts | Shapes / weighted steps |
| --- | --- | --- | --- |
| `atom.json` | | | 18 / 2,401 |
| `reactor.json` | | | 15 / 1,292 |
| `dot_halo.json` | | | 35 / 5,668 |
| `voice_pills.json` | | | 5 / 887 |
| `sentinel.json` | | tall, square | 144 / 18,303 |
| `phosphor.json` | | tall, square | 299 / 12,625 |
| `light_disc.json` | `tear_free` | | 113 / 4,900 |
| `lens.json` | | | 15 / 1,453 |
| `iris.json` | | | 109 / 9,943 |

Redrawn pixels per frame (the README's table, generated by `lab --table`):
Dot Halo, Lens, Phosphor and Sentinel redraw less than their built-in styles
because only changed elements are invalidated; Atom and Reactor about the
same; Iris (listening, 49k against 27k), Light Disc (answering, 155k against
133k) and Voice Pills (thinking, 38k against 25k) redraw more in one phase.

Known differences: random parts take other random numbers than the built-in
styles (Phosphor's noise, Light Disc's trail turns). Light Disc keeps each
trail's last 9 corners (built-in: 12), so a trail that turns 10 times within
its length is cut short. Circles snap to half pixels where the built-in
objects use whole ones.

## On a panel (test builds)

Building with `--define HOMETILES_ASSIST_DATA_SCREENS=1` compiles the engine,
the nine built-in copies and the overlay path:

- Each time the Assist screen opens, the overlay loads the screen file of the
  style picked in Settings: `/assist/<id>.json` on the microSD card (upload it
  with the Web Admin file manager), else in the internal storage, else the
  built-in copy. The load time and the file's worst-case cost are logged.
- A refused file falls back to the compiled style and logs the reason
  (`[Assist] Screen file <id> (microSD) refused, using the built-in style:
  ...`); the panel never shows a broken screen.
- The 108 KB block and a 24 KB read buffer come from PSRAM on first use and
  are kept. Frames run in the overlay's LVGL timer like the built-in styles,
  with the same easing, pacing and tear-free rules; the painter is one LVGL
  object whose areas are invalidated from `dirty()`.
- A frame over the fill budget logs once per showing.

## Integration contract (for a release)

- Ownership: the overlay owns the `Screen` block and the read buffer for the
  panel's lifetime (PSRAM, about 132 KB together); nothing else uses them.
- Threading: `load()`, `reset()`, `frame()`, `dirty()` and `draw()` run only
  in the LVGL task (the overlay's timer and its painter's draw event), like
  every other UI path; no worker touches the engine.
- Loading: at most 24 KB parsed per showing, to be measured on the S3 and P4.
  Files could be compiled once when uploaded instead.
- Fallback: a missing or refused file always leaves the compiled style, with
  one English log line; the built-in styles stay in the firmware until every
  maintainer board has run the files.
- Flash: the engine and the nine built-in copies (26 KB) are only in builds
  with the define (measured below).

## Measurements

| Profile | Image without the define | With the define | Difference |
| --- | --- | --- | --- |
| `waveshare_7b` | 5,182,176 bytes | 5,250,544 bytes | +68,368 bytes (engine ~42 KB, the nine files 26 KB) |

Static RAM grows by 16 bytes (the engine's block comes from PSRAM at runtime). S3
profiles are not measured yet.

Still needed on hardware: milliseconds per frame and per load on the S3 and
the P4 for every file, internal/PSRAM minimum free while the screen shows, and
whether the engine's hot data (slots, code, draw list) should sit in internal
RAM on the S3 instead of PSRAM.

## The lab

The lab runs the engine with the firmware's LVGL 9.6 renderer, fonts and the
overlay's frame loop and easing, in a window (SDL2) or headless to PNG files.
It also links the built-in styles, so it can show a file next to the style of
the same id.

### Build

Prerequisites: Node, git, a C/C++ compiler (gcc or clang) with `ar`, and SDL2
for the window. Without SDL2 the lab builds headless only.

- Linux or WSL: `sudo apt install build-essential libsdl2-dev`. Tested on
  WSL2 (Ubuntu 24.04, g++ 13.3, SDL2 2.30, window through WSLg). Use Node 24
  for the repo's tests: Node 26 breaks the web asset check.
- Windows: clang or gcc with `ar` (or `llvm-ar`) on PATH; the window needs
  SDL2 found by `pkg-config`, else the lab builds headless.

```sh
node tools/assist-screen-lab/build.mjs
```

LVGL 9.6.0 is found in this order: `LVGL_INCLUDE`,
`~/Documents/Arduino/lvgl-9.6.0/lvgl` (the Windows firmware build), or a
shallow clone of `v9.6.0` into `build/assist-screen-lab/lvgl-9.6.0`. LVGL and
the fonts are compiled once into a cached archive (`tools/lib/lvgl-host.mjs`,
shared with the host tests). Output: `build/assist-screen-lab/lab`. The build
should print no compiler warnings (the render test checks).

### Run

```sh
build/assist-screen-lab/lab                   # all screens in screens/
build/assist-screen-lab/lab --size=480x800    # start at another panel size
build/assist-screen-lab/lab my-screens/       # another folder, or single files
```

The window has a button bar: one button per screen; Auto, Listening, Thinking,
Answering; Built-in (the compiled style with the same id); Voice (simulated,
or hold the mouse: higher is louder); `<` size `>` through 1024x600, 1280x800,
800x480, 1280x720, 720x720, 480x800 and 720x1280. The numbers row shows
worst-case shapes and steps, pixels redrawn per frame (the panel's main cost),
time per frame on this computer (a rough guide only), the screen's shape and
whether the file has a layout for it, and a warning when the file needs a wide
screen the panel would not offer it on. Keys: arrows or Tab switch screens,
`1` `2` `3` phases, `A` auto, `B` built-in, `V` voice, `[` `]` size, `R`
reload, `S` screenshot, `Esc` quit. Saving a file reloads it.

Headless:

```sh
build/assist-screen-lab/lab tools/assist-screen-lab/screens --png=out --compare --frames [--size=WxH]
build/assist-screen-lab/lab tools/assist-screen-lab/screens --png=out --table
```

The first writes one PNG per screen and phase (the built-in style on the right
with `--compare`) and prints time and redrawn pixels per frame; `--table`
prints the README's results table. Runs repeat exactly: the simulated voice
and LVGL's `lv_rand` start from fixed seeds. `LAB_DIRTY=1` prints every
redraw area.

### Test

```sh
LVGL_INCLUDE=build/assist-screen-lab/lvgl-9.6.0 node tools/run-tests.mjs assist
```

(`LVGL_INCLUDE` only when the lab build cloned LVGL; the tests also look in
`~/Documents/Arduino/lvgl-9.6.0/lvgl`.)

- `test-assist-screen-files.mjs` compiles the engine with ASan/UBSan where the
  compiler has them, loads every file and its built-in copy, runs 400 frames
  through every phase checking the worst case holds, runs the mutation pass,
  and checks refusals (limits, bad formulas, unknown keys, name clashes, bad
  layouts, format versions, long text), history, cost weights, layouts and
  the fill budget against hostile files.
- `test-assist-screen-render.mjs` builds the lab, renders every screen at four
  sizes next to its built-in style, compares the pixels, and checks that a
  second run gives the same pixels.

Both report `SKIP` without LVGL 9.6.0 or a compiler.

## Making or changing a screen

1. Copy the closest file in `screens/`, give it a new `id` and `name`.
2. Run the lab on it; save to reload. Use Built-in to compare with a style.
3. Step through the sizes. Put positions a narrow screen must move into vars
   and give `tall` and `square` layouts where needed.
4. Check the numbers row stays well inside the limits and the redrawn pixels
   stay low: give parts that move independently their own top-level element.
5. Run the tests. For a built-in id, regenerate the firmware copies with
   `node tools/generate-assist-screens.mjs`.

## Toward a release

1. Measure the test build on every maintainer board (above).
2. Store uploaded files in LittleFS (`/assist/<id>.json`), compiled once when
   uploaded; the Web Admin uploads them or fetches them from a gallery.
3. List file screens with new ids after the built-in ones. Their `needs` feed
   `assist_style_catalog`, so Settings and the Bridge select only offer
   screens the panel can show.
4. Only after hardware sign-off on every board: replace the compiled styles
   with their files, which frees their flash.

Open questions: where downloadable screens come from (a gallery, the Bridge,
a URL), and whether a panel keeps several files or one active screen.
