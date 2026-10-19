# Assist Screen Lab

A desktop prototype for Assist screens loaded from data instead of compiled
into the firmware. A screen is a JSON file ([FORMAT.md](FORMAT.md)). A small
engine (`engine/assist_screen.*`) compiles the file, checks it against the
panel's limits, and draws it every frame with LVGL 9.6.

The lab runs that engine on Linux, WSL or Windows with the firmware's own LVGL
version, fonts and frame loop. It can also show the built-in style of the same
id beside the file, for comparison.

Nothing here is in the firmware yet. The engine is written to move into
`src/ui/assist/` unchanged: it has no Arduino, no allocation after `init()`,
and LVGL draw calls only.

## Build

You need a C++ compiler (gcc or clang), `ar`, git and Node.

- Linux or WSL: `sudo apt install build-essential libsdl2-dev`. SDL2 is only
  needed for the window. Without it the lab builds headless (PNG output only).
- Run `node tools/assist-screen-lab/build.mjs` from the repo root.

The build finds LVGL 9.6.0 in this order: `LVGL_INCLUDE`, then
`~/Documents/Arduino/lvgl-9.6.0/lvgl`, otherwise it clones it into
`build/assist-screen-lab/lvgl-9.6.0`. The lab is written to
`build/assist-screen-lab/lab`.

## Run

```sh
build/assist-screen-lab/lab tools/assist-screen-lab/screens/sentinel.json
build/assist-screen-lab/lab tools/assist-screen-lab/screens/iris.json --size=720x720
```

| Key | Action |
| --- | --- |
| `1` `2` `3` | Listening, thinking, answering |
| `A` | Cycle the phases automatically (default) |
| `V` | Simulated voice on/off; when off, hold the mouse button, higher means louder |
| `B` | Built-in style with the same id, for comparison |
| `[` `]` | Previous or next panel size (1024x600, 1280x800, 800x480, 1280x720, 720x720, 480x800, 720x1280) |
| `R` | Reload; saving the file also reloads it |
| `S` | Screenshot |
| `Esc` | Quit |

The title bar shows the numbers that matter on the panel:

- shapes and expression steps per frame (worst case, from the file)
- pixels redrawn per frame
- milliseconds per frame on this computer, which is only a rough guide to the
  panel

Headless, without a window:

```sh
lab <file> --png=out --compare --frames [--size=WxH]
```

This writes one PNG per phase, with the built-in style on the right when
`--compare` is given, and prints time and redrawn pixels per frame for both.
`LAB_DIRTY=1` also prints every redraw area.

## Results (1024x600, the 7B)

The three wide or heavy styles were ported to files. All three match the
built-in styles in every phase and at 800x480, 1280x800, 720x720, 480x480 and
720x1280.

| Screen | Worst case shapes / steps per frame | Redrawn px/frame, file vs built-in (listen / think / answer) |
| --- | --- | --- |
| Sentinel | 144 / 15,183 | 133k / 125k / 127k vs 218k / 181k / 178k |
| Iris | 109 / 8,183 | 49k / 129k / 108k vs 38k / 128k / 118k |
| Phosphor | 299 / 9,147 | 187k / 99k / 134k vs 272k / 281k / 280k |

Redrawn pixels are what the panel has to paint, which is its main cost.
The files redraw less than the built-in styles because only the elements whose
drawing changed are redrawn. The panel's limits are 1200 shapes and 60,000
steps.

## Device limits

Every limit is checked at load, so a file the lab accepts cannot exceed them
on the panel:

- fixed memory (one block of about 145 KB)
- bounded file size, values, expression code, names, elements, polylines and
  text
- worst-case shapes and expression steps per frame
- only the panel's font sizes
- NaN and infinity become 0; coordinates are clamped

See the table in [FORMAT.md](FORMAT.md#panel-limits).
`tools/tests/assist/test-assist-screen-files.mjs` loads the three files, runs
them through every phase and checks that broken files are refused.

## Next steps for the firmware (not done)

1. Move `engine/` into `src/ui/assist/` and let the overlay draw a loaded
   screen in place of a built-in style.
2. Store files in LittleFS (`/assist/<id>.json`). The Web Admin uploads them,
   or downloads them from a gallery page, and the panel compiles each one
   before keeping it.
3. List file screens after the built-in ones. Their `needs` feed
   `assist_style_catalog`, so the Settings list and the Bridge select offer
   only screens the panel can show.
