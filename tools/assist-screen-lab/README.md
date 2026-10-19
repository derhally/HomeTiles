# Assist Screen Lab

A desktop prototype for Assist screens loaded from data instead of compiled
into the firmware. A screen is a JSON file ([FORMAT.md](FORMAT.md)). A small
engine (`engine/assist_screen.*`) compiles the file, checks it against the
panel's limits, and draws it every frame with LVGL 9.6.

The lab runs that engine on Linux, WSL or Windows with the firmware's own LVGL
version, fonts and frame loop. It can also show the built-in style of the same
id beside the file, for comparison.

How it works, why, and the plan for the firmware: [docs-dev/assist-screens.md](../../docs-dev/assist-screens.md).

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
build/assist-screen-lab/lab                      # every screen
build/assist-screen-lab/lab --size=720x720       # start at another panel size
build/assist-screen-lab/lab my-screens/          # your own folder of .json screens
```

The window lists all nine Assist screens in a button bar under the screen.
Every one is a file now; a built-in style without a file would show dimmer,
with the firmware's own drawing.

| Bar row | Buttons |
| --- | --- |
| Screens | One button per screen |
| Controls | Auto (cycles the phases), Listening, Thinking, Answering; Built-in (switches a file screen to the built-in style with the same id, for comparison); Voice (simulated, or hold the mouse on the screen: higher means louder); `<` size `>` (1024x600, 1280x800, 800x480, 1280x720, 720x720, 480x800, 720x1280) |
| Numbers | For a file: worst-case shapes and steps per frame, pixels redrawn per frame and time per frame on this computer; then the screen's shape (wide, square, tall) and whether the file has a layout for it ([FORMAT.md](FORMAT.md#layouts)), with a warning when the file needs a wide screen the panel would not offer it on. Redrawn pixels are the panel's main cost; time on this computer is only a rough guide. |

Saving a screen file reloads it. Keys do the same as the buttons:

| Key | Action |
| --- | --- |
| `←` `→` (or `Tab`, `Shift+Tab`) | Previous or next screen |
| `1` `2` `3` / `A` | Listening, thinking, answering / auto cycle |
| `B` | Built-in comparison |
| `V` | Simulated voice on or off |
| `[` `]` | Previous or next panel size |
| `R` | Reload |
| `S` | Screenshot (`lab-shot-N.png`) |
| `Esc` | Quit |

Headless, without a window:

```sh
lab [file or folder ...] --png=out --compare --frames [--size=WxH]
```

This writes one PNG per phase, with the built-in style on the right when
`--compare` is given, and prints time and redrawn pixels per frame for both.
`LAB_DIRTY=1` also prints every redraw area.

## Results (1024x600, the 7B)

Every built-in style has a file. Each one matches its built-in style in every
phase, also at 800x480, 1280x800, 720x720 and 480x800. Light Disc's trails turn
at random, so where they run differs from run to run, as it does on the panel.
Atom, Reactor and Dot Halo shade their balls with `grad`.

| Screen | Worst case shapes / steps per frame | Redrawn px/frame, file vs built-in (listen / think / answer) |
| --- | --- | --- |
| Sentinel | 144 / 15,183 | 133k / 125k / 127k vs 218k / 181k / 178k |
| Iris | 109 / 8,183 | 49k / 129k / 108k vs 27k / 128k / 118k |
| Phosphor | 299 / 9,147 | 187k / 99k / 134k vs 272k / 281k / 280k |
| Atom | 18 / 1,755 | 225k / 171k / 235k vs 224k / 171k / 233k |
| Reactor | 15 / 1,278 | 216k / 151k / 163k vs 211k / 148k / 161k |
| Dot Halo | 35 / 4,633 | 165k / 101k / 167k vs 281k / 519k / 276k |
| Voice Pills | 5 / 766 | 99k / 38k / 98k vs 96k / 25k / 96k |
| Light Disc | 105 / 5,433 | 241k / 105k / 155k vs 377k / 120k / 133k |
| Lens | 15 / 1,420 | 82k / 74k / 75k vs 87k / 80k / 79k |

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
`tools/tests/assist/test-assist-screen-files.mjs` loads every file in `screens/`, runs
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
