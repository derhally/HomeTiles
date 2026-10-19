# Assist Screens from Files

The animated screen HomeTiles shows while Assist listens, thinks and answers can come from a small JSON file instead of the firmware. Anyone can make a new look, try it in a desktop lab, and put it on a panel without building firmware.

!!! warning "Test builds only"
    Screens from files are a preview. They are only in firmware built with `HOMETILES_ASSIST_DATA_SCREENS=1`; release builds use the built-in styles. Not every board has been measured yet.

## The Built-in Styles as Files { data-toc-label="Built-in styles" }

All nine styles in **Settings → Assist** exist as files that draw the same picture as the built-in style: Atom, Reactor, Dot Halo, Voice Pills, Sentinel, Phosphor, Light Disc, Lens and Iris.

<figure class="ht-screenshot">
<img src="../images/assist-screens-styles.png" alt="The nine Assist screen styles while thinking: Atom, Reactor, Dot Halo, Voice Pills, Sentinel, Phosphor, Light Disc, Lens and Iris" width="1220" height="725" loading="lazy">
<figcaption>The nine styles drawn from their files (thinking)</figcaption>
</figure>

Sentinel and Phosphor also bring layouts for square and portrait screens, so their readouts move instead of overlapping the rings.

## New Looks { data-toc-label="New looks" }

Two example screens show what a file can do beyond the built-in styles.

**Robot**: a small robot in outline. Listening, it bobs, blinks and sound waves reach its ears with your voice. Thinking, its eyes wander, a hand goes to its chin and dots bounce above it. Answering, its mouth moves with the reply, its eyes turn happy and it waves.

<figure class="ht-screenshot">
<img src="../images/assist-screens-robot.png" alt="Robot screen while listening, thinking and answering" width="912" height="360" loading="lazy">
<figcaption>Robot: listening, thinking, answering</figcaption>
</figure>

**Retro Robot**: a tall tin-can head with a visor and a grille mouth. Its eyes glance about while listening, its lids droop and a question mark bobs while thinking, and the grille teeth part with each word while answering.

<figure class="ht-screenshot">
<img src="../images/assist-screens-retro-robot.png" alt="Retro Robot screen while listening, thinking and answering" width="912" height="360" loading="lazy">
<figcaption>Retro Robot: listening, thinking, answering</figcaption>
</figure>

## Put a Screen on the Panel { data-toc-label="Use a screen file" }

On a test build, each time the Assist screen opens, the panel draws the style picked in **Settings → Assist** from its file:

1. Format a microSD card as FAT32 and insert it before switching the panel on.
2. In the Web Admin file manager, create an `assist` folder on the card.
3. Upload the screen as `<style>.json`, for example `atom.json` to replace Atom with the Robot.
4. Pick that style in **Settings → Assist** and tap **Preview**.

Without a file, the panel uses its built-in copy of the style. A file the panel cannot use is ignored and the built-in style shows instead; the serial log says why. Delete the file to go back.

## Make Your Own { data-toc-label="Make a screen" }

A screen is one JSON file of a few kilobytes: colors, a few named values and a list of shapes (lines, arcs, discs, boxes, lines through points and short readouts) whose sizes, positions and colors are small formulas of time, voice level and phase. The format is described in `tools/assist-screen-lab/FORMAT.md` in the repository.

The **Assist Screen Lab** draws screens on a computer with the same graphics library, fonts and timing as the panel:

```sh
node tools/assist-screen-lab/build.mjs
build/assist-screen-lab/lab
```

It needs Node, git, a C++ compiler and, for the window, SDL2 (Linux or WSL: `sudo apt install build-essential libsdl2-dev`). Buttons switch screens, phases and panel sizes, show the built-in style for comparison, and report what a screen costs per frame. Saving the file reloads it.

## Limits { data-toc-label="Limits" }

A file can only draw shapes; it cannot run code or read Home Assistant data. The panel checks every file before it uses it:

- at most 24 KB, 1,200 shapes and a fixed amount of work per frame
- one fixed block of memory, about 108 KB
- at most six screens' worth of drawing per frame; anything beyond is left out
- the panel's own font sizes, and short readouts only: words such as "Listening…" come from the panel's translations

A file over a limit is refused with a message naming the problem, and the built-in style stays.
