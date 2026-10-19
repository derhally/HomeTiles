# Assist screens from data files

Design reference for Assist screens described by data instead of compiled
into the firmware, and for the Assist Screen Lab that develops and checks
them on a desktop. Status: lab only. The engine, the nine screen files and the
lab live under `tools/assist-screen-lab/`; nothing runs on a panel yet.

| Part | Where |
| --- | --- |
| File format reference | `tools/assist-screen-lab/FORMAT.md` |
| Engine (compile, frame, redraw, draw) | `tools/assist-screen-lab/engine/assist_screen.*` |
| Screen files, one per built-in style | `tools/assist-screen-lab/screens/*.json` |
| Desktop lab (window and headless) | `tools/assist-screen-lab/lab.cpp`, `build.mjs`, `README.md` |
| Host test | `tools/tests/assist/test-assist-screen-files.mjs` |
| Built-in styles it reproduces | `src/ui/assist/assist_styles*.cpp`, `assist_style_catalog.h`, `assist_overlay.cpp` |

## Why

The Assist overlay offers nine styles (Atom, Reactor, Dot Halo, Voice Pills,
Sentinel, Phosphor, Light Disc, Lens, Iris), each hand-written C++ against
LVGL. A new style or a tweak needs a firmware build and an OTA update, and
every style costs flash on every panel. A data file can instead be added,
shared and changed without firmware, and only the panels that use it carry it.

The goal is files that look exactly like the built-in styles, cost no more on
the panel, and cannot hurt the panel however they are written, because a file
may come from a download.

## How a screen runs

A file is a JSON object with colours, named values (vars), optional layouts
per screen shape, and a list of shapes (items) whose properties are small
formulas. The engine works in three steps.

1. **Load (once).** A bounded JSON parser builds a fixed DOM over the file
   text. Every formula is compiled (Pratt parser) to a small stack-machine
   program in one fixed code array. Names resolve to slots: built-in inputs,
   vars, per-element lets and repeat indexes. The engine counts the worst case
   per frame over every branch and repeat (repeat counts are constants) and
   refuses the file if it is over any limit, with a message naming the problem
   and its line. Afterwards the file text is not needed.
2. **Frame (every 33 ms).** The host sets the inputs, the engine steps the
   vars in order, then runs each element's formulas for each repeat into a
   flat draw list: lines (also dashed), arcs and rings, discs (optionally with
   a vertical gradient), filled and outlined rectangles, polylines with fading
   trail copies, text and scrolling text rows. Coordinates are clamped and
   snapped to half pixels, so movement too small to see draws the same list.
3. **Redraw and draw.** Each top-level element keeps a hash of its commands
   and the box it covered this frame and the frame before. Only the boxes of
   elements whose drawing changed are invalidated (joined, at most 8 areas); a
   frame with no visible change invalidates nothing. One LVGL draw callback
   replays the list with `lv_draw_line/arc/rect/label`, the calls the line
   styles already use.

The inputs are what the overlay already feeds the built-in styles
(`assist_styles::Frame`), so a file screen drops in where a style is drawn:

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
`next`) carry anything that must remember: easing, timers, ring buffers of
corners (Light Disc's trails).

## Limits: why a file cannot hurt the panel

Every limit is checked at load (`engine/assist_screen.h`, table in
`FORMAT.md`): file size 24 KB, JSON values and nesting, compiled code 4096
steps, 192 names, 160 elements, repeat 256, groups 4 deep, polylines and their
point pool, 24 text buffers of 48 bytes, at most 1,200 shapes and 60,000
formula steps per frame in the worst case. One `Screen` is a fixed block of
about 145 KB (PSRAM on the panel) allocated once; nothing is allocated per
load or per frame. NaN and infinity become 0. A file can only describe shapes:
there are no loops of variable length, no user functions, no access to Home
Assistant data, touch or storage, and only the font sizes every panel carries.
Unknown keys, unknown shapes, a var named like a colour or a built-in input,
and layouts that change state vars are refused instead of guessed.

## Screen shapes and layouts

Readouts with a fixed pixel size beside the rings fit a wide screen and
collide with the rings on a narrow one. The engine classifies the screen like
`assist_style_catalog` does (`kWideAspect` 1.2): `wide` (at least 1.2 times as
wide as tall: 1024x600, 800x480, 1280x800), `tall` (480x800, 720x1280) or
`square` (720x720, 480x480).

A file may add `"layouts": {"tall": {...}, "square": {...}}`. Per shape it can
replace `reach` keys (how far the screen reaches sideways, which sets `r`) and
give derived vars another formula; elements can be limited to shapes with
`"on": ["wide"]`. Positions a layout may move therefore live in vars. A shape
without a layout uses the file as written. `Info.reach[shapeOf(w, h)]` is the
reach the host uses for `r` (`assist_styles::baseRadius` with the file's
values).

Sentinel and Phosphor use this. Sentinel keeps its side readouts on wide
screens, puts them above and below the rings on tall ones and in the top
corners on square ones; Phosphor narrows its scope on tall and square screens
instead of shrinking. Both therefore drop the built-in styles' `needs: wide`.

## The screen files

All nine built-in styles are files and match them in every phase at wide,
square and tall sizes (rendered side by side by the lab).

| File | Needs | Layouts | Worst case shapes / steps per frame | Notes |
| --- | --- | --- | --- | --- |
| `atom.json` | | | 18 / 1,755 | Shaded lobes with `grad` |
| `reactor.json` | | | 15 / 1,278 | Sonar rings: four birth times as state vars |
| `dot_halo.json` | | | 35 / 4,633 | |
| `voice_pills.json` | | | 5 / 766 | |
| `sentinel.json` | | tall, square | 144 / 15,213 | Readouts move per shape |
| `phosphor.json` | | tall, square | 299 / 9,022 | Narrower scope per shape |
| `light_disc.json` | `tear_free` | | 105 / 5,433 | Trails keep their last 5 corners (built-in: 12) |
| `lens.json` | | | 15 / 1,420 | |
| `iris.json` | | | 109 / 8,183 | |

At 1024x600 the files redraw as many pixels per frame as the built-in styles
or fewer (Dot Halo thinking 101k against 519k, Light Disc listening 241k
against 377k), because only changed elements are invalidated. The README keeps
the full table.

Known differences: Light Disc's trails turn at random like the built-in ones,
so they run elsewhere, and with 5 instead of 12 remembered corners a trail
that turns 6 times within its length is cut short. Circles snap to half
pixels where the built-in objects use whole ones.

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
prints compiler warnings; it should print none.

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
```

This writes one PNG per screen and phase (the built-in style on the right with
`--compare`) and prints time and redrawn pixels per frame for file and
built-in. Runs are repeatable: the simulated voice and LVGL's `lv_rand` start
from fixed seeds in every pass. `LAB_DIRTY=1` prints every redraw area.

### Test

```sh
LVGL_INCLUDE=build/assist-screen-lab/lvgl-9.6.0 node tools/run-tests.mjs assist
```

(`LVGL_INCLUDE` only when the lab build cloned LVGL; the test also looks in
`~/Documents/Arduino/lvgl-9.6.0/lvgl`.)

`test-assist-screen-files.mjs` compiles the engine against the host LVGL,
loads every file, runs 400 frames through every phase checking the worst case
holds, and checks that broken files (over limits, bad formulas, unknown keys,
name clashes, bad layouts) are refused and that layouts and `on` apply. It
reports `SKIP` without LVGL 9.6.0 or a compiler.

## Making or changing a screen

1. Copy the closest file in `screens/`, give it a new `id` and `name`.
2. Run the lab on it; save to reload. Use Built-in to compare with a style.
3. Step through the sizes. Put positions a narrow screen must move into vars
   and give `tall` and `square` layouts where needed.
4. Check the numbers row stays well inside the limits and the redrawn pixels
   stay low: give parts that move independently their own top-level element.
5. Run the test and the headless lab at the sizes you changed.

## Firmware integration (not done)

1. Move `engine/` into `src/ui/assist/`. The overlay keeps its frame loop and
   easing and draws a loaded screen from one painter object instead of a
   built-in style's objects; `r` comes from `Info.reach[shapeOf(w, h)]`.
2. Store files in LittleFS (`/assist/<id>.json`). The Web Admin uploads them
   or fetches them from a gallery; the panel compiles each one before keeping
   it, and shows the error message otherwise.
3. List file screens after the built-in ones. Their `needs` feed
   `assist_style_catalog`, so Settings and the Bridge select only offer
   screens the panel can show.
4. Later, the built-in styles can become bundled files, which frees their
   flash.

Open questions: where downloadable screens come from (a gallery, the Bridge,
a URL), whether a panel keeps several files or one active screen, and hardware
measurements on the S3 and P4 panels; the lab's milliseconds are only a
desktop guide.
