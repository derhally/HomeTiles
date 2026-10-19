# Assist Screen Lab

A desktop prototype for Assist screens loaded from data instead of compiled
into the firmware. A screen is a JSON file ([FORMAT.md](FORMAT.md)). A small
engine (`src/ui/assist/assist_screen.*`) compiles the file, checks it against
the panel's limits, and draws it every frame with LVGL 9.6.

The lab runs that engine on Linux, WSL or Windows with the firmware's own LVGL
version, fonts and frame loop. It can also show the built-in style of the same
id beside the file, for comparison.

How it works, why, and the plan for the firmware: [docs-dev/assist-screens.md](../../docs-dev/assist-screens.md).

The engine is in the firmware tree but compiled only into test builds with
`HOMETILES_ASSIST_DATA_SCREENS=1` (see the design document); it has no
Arduino, no allocation after `init()`, and LVGL draw calls only.

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
lab tools/assist-screen-lab/screens --png=out --table     # the Results table below
```

This writes one PNG per phase, with the built-in style on the right when
`--compare` is given, and prints time and redrawn pixels per frame for both.
`--table` prints one row of the Results table per screen instead. Runs repeat
exactly: the simulated voice and LVGL's `lv_rand` start from fixed seeds.
`LAB_DIRTY=1` also prints every redraw area.

## Results (1024x600, the 7B)

Every built-in style has a file. In the render test the files match their
built-in styles in every phase at 1024x600, 800x480, 480x480 and 720x1280
(Sentinel and Phosphor on wide sizes only, since their square and tall layouts
differ on purpose): at most 6 % of the pixels differ, from the random parts
(Phosphor's noise, Light Disc's trails). Atom, Reactor and Dot Halo shade their
balls with `grad`.

Generated with `lab tools/assist-screen-lab/screens --png=out --table`:

| Screen | Worst case shapes / steps per frame | Redrawn px/frame, file vs built-in (listen / think / answer) |
| --- | --- | --- |
| Atom | 18 / 2,401 | 225k / 171k / 235k vs 224k / 171k / 233k |
| Dot Halo | 35 / 5,668 | 165k / 101k / 167k vs 281k / 519k / 276k |
| Iris | 109 / 9,943 | 49k / 129k / 108k vs 27k / 128k / 118k |
| Lens | 15 / 1,453 | 82k / 74k / 75k vs 87k / 80k / 79k |
| Light Disc | 113 / 4,900 | 241k / 105k / 155k vs 377k / 120k / 133k |
| Phosphor | 299 / 12,625 | 187k / 99k / 134k vs 272k / 281k / 280k |
| Reactor | 15 / 1,292 | 216k / 151k / 163k vs 211k / 148k / 161k |
| Sentinel | 144 / 18,303 | 133k / 125k / 127k vs 218k / 181k / 178k |
| Voice Pills | 5 / 887 | 99k / 38k / 98k vs 96k / 25k / 96k |

Redrawn pixels are what the panel has to paint, which is its main cost. Dot
Halo, Lens, Phosphor and Sentinel redraw less than their built-in styles,
because only the elements whose drawing changed are redrawn; Atom and Reactor
about the same; Iris (listening), Light Disc (answering) and Voice Pills
(thinking) redraw more in one phase each. Steps are weighted (FORMAT.md); the
limits are 1,200 shapes and 60,000 steps. Milliseconds on a desktop say little
about a panel: the design document lists what still needs measuring on S3 and
P4 hardware.

Two more screens, `robot.json` and `retro_robot.json`, are examples of new
looks (pictures in the design document); they have no built-in style to
compare with.

## Device limits

The limits are checked at load, and the painted area as each frame is built,
so a file the lab accepts cannot exceed them on the panel:

- fixed memory (one block of about 108 KB)
- bounded file size, values, expression code, names, elements, polylines,
  history and text
- worst-case shapes and weighted expression steps per frame
- at most 6 screens' worth of painted area per frame; radii clamped to the
  screen diagonal
- only the panel's font sizes; fixed text parts of at most 8 bytes
- NaN and infinity become 0; coordinates are clamped

See the table in [FORMAT.md](FORMAT.md#panel-limits). Tests:

- `tools/tests/assist/test-assist-screen-files.mjs` loads every file, runs it
  through every phase, checks that broken and hostile files are refused or
  stay within the budget, and runs a few thousand mutated copies of the nine
  files under ASan/UBSan.
- `tools/tests/assist/test-assist-screen-render.mjs` builds the lab, renders
  every screen at four sizes next to its built-in style and compares the
  pixels, and checks that a second run repeats them exactly.

## On a panel (test builds)

Firmware built with `--define HOMETILES_ASSIST_DATA_SCREENS=1` draws the style
picked in Settings from its screen file: `/assist/<id>.json` on the microSD
card (upload it with the Web Admin file manager), else in the internal
storage, else the built-in copy (`src/ui/assist/assist_screen_files.h`,
generated by `node tools/generate-assist-screens.mjs`). The file is read each
time the screen opens. A file that is refused falls back to the compiled style
and logs why (`[Assist] Screen file ... refused`).
