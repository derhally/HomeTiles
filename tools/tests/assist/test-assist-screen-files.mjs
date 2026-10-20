// Host regression for Assist screens from data (tools/assist-screen-lab):
// every built-in style's file (screens/*.json) loads within the panel limits and runs
// frames through every phase, and files that break a limit are refused with
// a message instead of loading (a downloaded file must not hurt the panel).
// Rendering is compared with the built-in styles by the lab itself
// (lab <file> --png=DIR --compare --frames).
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const lab = path.join(root, 'tools/assist-screen-lab');
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: assist screen files test needs LVGL 9.6.0 (LVGL_INCLUDE) and a host compiler');
  process.exit(0);
}

const program = String.raw`
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "src/ui/assist/assist_screen.h"
#include "src/ui/assist/assist_screen_files.h"

// Load checks only need to know which sizes exist.
const lv_font_t* assist_screen_font(int px) {
  static lv_font_t font;
  return (px == 16 || px == 20 || px == 24 || px == 28 || px == 32 || px == 40 || px == 48) ? &font : nullptr;
}

static std::vector<char> memory(assist_screen::screenSize());
static assist_screen::Screen* screen = assist_screen::init(memory.data());
static char error[160];

static bool loadText(const std::string& text) {
  return assist_screen::load(screen, text.data(), text.size(), error, sizeof(error));
}

static void refused(const std::string& text, const char* expected) {
  if (loadText(text)) { printf("FAIL: loaded, expected '%s'\n", expected); exit(1); }
  if (!strstr(error, expected)) { printf("FAIL: '%s', expected '%s'\n", error, expected); exit(1); }
}

static std::string file(const std::string& items, const std::string& extra = "") {
  return "{\"format\": 1, \"id\": \"t\"" + extra + ", \"items\": [" + items + "]}";
}

int main(int argc, char** argv) {
  for (int a = 1; a < argc; ++a) {
    FILE* f = fopen(argv[a], "rb");
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    if (!loadText(text)) { printf("FAIL: %s: %s\n", argv[a], error); return 1; }
    const assist_screen::Info& in = assist_screen::info(screen);
    if (in.frame_cmds > assist_screen::kMaxFrameCmds || in.frame_ops > assist_screen::kMaxFrameOps) return 1;
    assist_screen::reset(screen, 7);
    // Listening, thinking, answering at 1024x600 with a moving voice level.
    uint32_t most = 0;
    for (int i = 0; i < 400; ++i) {
      const int phase = i / 134;
      assist_screen::Inputs inputs{i / 30.0f, 1 / 30.0f, 0.5f + 0.5f * sinf(i * 0.3f), phase ? 1.0f : 0.0f, phase == 2,
                                   102.0f * fminf(1.0f, i / 10.0f), 512, 270, 1024, 600};
      assist_screen::frame(screen, inputs);
      const uint32_t cmds = assist_screen::commandCount(screen);
      if (cmds > in.frame_cmds) { printf("FAIL: %s drew %u shapes, worst case %u\n", in.id, cmds, in.frame_cmds); return 1; }
      if (cmds > most) most = cmds;
      lv_area_t areas[8];
      const int dirty = assist_screen::dirty(screen, areas, 8);
      if (dirty < 0 || dirty > 8) return 1;
      if (i == 0 && dirty == 0) { printf("FAIL: %s: first frame not drawn\n", in.id); return 1; }
    }
    if (most == 0) { printf("FAIL: %s drew nothing\n", in.id); return 1; }
    printf("%s %u %u %u\n", in.id, in.frame_cmds, in.frame_ops, most);
    char id[40];
    snprintf(id, sizeof(id), "%s", in.id);  // in is overwritten by the loads below
    // Broken copies of a real file are refused or run safely (the test runs
    // under ASan/UBSan where the compiler has them).
    uint32_t seed = 0x9E3779B9u ^ static_cast<uint32_t>(text.size());
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    const char* tokens[] = {"{", "}", "[", "]", "(", "\"", "1e38", "-", "99999999", "\\", ":", ",", "nan", "{v:99d}"};
    int loaded = 0;
    const char* extremes[] = {"0", "-1", "1e9", "-1e9", "1e-9", "4000", "0.000001", "65535", "3.4e38"};
    for (int m = 0; m < 400; ++m) {
      std::string t = text;
      switch (m % 5) {
        case 4: {
          // Valid files with wild numbers: replace some numbers in the text.
          for (int k = 0; k < 1 + (m / 5) % 3; ++k) {
            size_t at = 40 + rnd() % (t.size() - 40);  // past "format": 1
            while (at < t.size() && !isdigit(static_cast<unsigned char>(t[at]))) ++at;
            size_t end = at;
            while (end < t.size() && (isdigit(static_cast<unsigned char>(t[end])) || t[end] == '.')) ++end;
            if (at < t.size()) t.replace(at, end - at, extremes[rnd() % 9]);
          }
          break;
        }
        case 0: t.resize(rnd() % t.size()); break;
        case 1: for (int k = 0; k < 6; ++k) t[rnd() % t.size()] = static_cast<char>(rnd() & 0xFF); break;
        case 2: for (int k = 0; k < 4; ++k) t.insert(rnd() % t.size(), tokens[rnd() % 14]); break;
        default: {
          const size_t a = rnd() % t.size(), b = rnd() % t.size();
          const size_t lo = a < b ? a : b, len = (a < b ? b - a : a - b) % 200;
          t.insert(rnd() % t.size(), t.substr(lo, len));
        }
      }
      if (!loadText(t)) continue;
      ++loaded;
      assist_screen::reset(screen, m);
      const float sizes[3][2] = {{1024, 600}, {480, 480}, {480, 800}};
      for (int i = 0; i < 30; ++i) {
        const float w = sizes[i % 3][0], h = sizes[i % 3][1];
        assist_screen::Inputs inputs{i / 30.0f, 1 / 30.0f, (i % 7) / 6.0f, i > 15 ? 1.0f : 0.0f, i > 22,
                                     80.0f, w / 2, h * 0.45f, w, h};
        assist_screen::frame(screen, inputs);
        lv_area_t areas[8];
        if (assist_screen::dirty(screen, areas, 8) > 8) return 1;
      }
    }
    printf("fuzz %s %d\n", id, loaded);
  }

  // Limits a file cannot pass.
  refused(std::string(assist_screen::kMaxFileBytes + 1, ' '), "limit");
  refused(file("{\"type\": \"line\", \"x1\": 0, \"y1\": 0, \"x2\": 1, \"y2\": 1, \"colour\": 1}"), "colour");
  refused(file("{\"type\": \"line\", \"repeat\": 300, \"x1\": 0, \"y1\": 0, \"x2\": 1, \"y2\": 1}"), "repeat");
  refused(file("{\"type\": \"group\", \"repeat\": 200, \"items\": [{\"type\": \"line\", \"repeat\": 200, "
               "\"x1\": 0, \"y1\": 0, \"x2\": 1, \"y2\": 1}]}"), "limit");
  refused(file("{\"type\": \"line\", \"x1\": \"nope\", \"y1\": 0, \"x2\": 1, \"y2\": 1}"), "nope");
  refused(file("{\"type\": \"text\", \"x\": 0, \"y\": 0, \"font\": 15, \"text\": \"a\"}"), "font");
  refused(file("{\"type\": \"line\", \"x1\": \"sin(\", \"y1\": 0, \"x2\": 1, \"y2\": 1}"), "line 1");
  refused("{\"format\": 1, \"id\": \"t\", \"items\": [[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]}", "");
  // Bad numbers draw nothing harmful: NaN and infinity become 0, coordinates
  // are clamped.
  if (!loadText(file("{\"type\": \"line\", \"x1\": \"0 / 0\", \"y1\": \"1 / 0\", \"x2\": \"1e30\", \"y2\": \"-1e30\", \"width\": 4}"))) {
    printf("FAIL: %s\n", error);
    return 1;
  }
  assist_screen::reset(screen, 1);
  assist_screen::frame(screen, assist_screen::Inputs{0, 0, 0, 0, false, 100, 0, 0, 100, 100});
  lv_area_t areas[8];
  const int n = assist_screen::dirty(screen, areas, 8);
  if (n != 1 || areas[0].x1 < -40000 || areas[0].x2 > 40000 || areas[0].y1 < -40000 || areas[0].y2 > 40000) {
    printf("FAIL: clamping\n");
    return 1;
  }
  // A name is a var or a color, never both (the color won silently).
  refused(file("", ", \"colors\": {\"core\": \"#E0F2FE\"}, \"vars\": {\"core\": 1}"), "already a color");
  refused(file("{\"type\": \"line\", \"let\": {\"core\": 1}, \"x1\": 0, \"y1\": 0, \"x2\": 1, \"y2\": 1}",
               ", \"colors\": {\"core\": \"#E0F2FE\"}"), "already a color");
  refused(file("", ", \"colors\": {\"r\": \"#E0F2FE\"}"), "already a name");
  // Layouts: per shape, reach and derived vars change; "on" hides elements.
  refused(file("", ", \"layouts\": {\"round\": {}}"), "round");
  refused(file("", ", \"layouts\": {\"tall\": {\"vars\": {\"nope\": 1}}}"), "nope");
  refused(file("", ", \"vars\": {\"k\": {\"init\": 0}}, \"layouts\": {\"tall\": {\"vars\": {\"k\": 1}}}"), "state");
  refused(file("{\"type\": \"disc\", \"on\": [\"round\"], \"x\": 0, \"y\": 0, \"radius\": 5}"), "shape");
  if (!loadText(file("{\"type\": \"disc\", \"x\": \"px\", \"y\": 50, \"radius\": 5},"
                     "{\"type\": \"disc\", \"on\": [\"tall\"], \"x\": \"20 + 100 * tall\", \"y\": 50, \"radius\": 5}",
                     ", \"vars\": {\"px\": 10}, \"reach\": {\"half_r\": 3},"
                     " \"layouts\": {\"tall\": {\"reach\": {\"half_r\": 2}, \"vars\": {\"px\": 60}}}"))) {
    printf("FAIL: %s\n", error);
    return 1;
  }
  const assist_screen::Info& lay = assist_screen::info(screen);
  if (lay.reach[assist_screen::kWide].half_r != 3 || lay.reach[assist_screen::kTall].half_r != 2 ||
      lay.layouts != (1u << assist_screen::kTall) || assist_screen::shapeOf(1024, 600) != assist_screen::kWide ||
      assist_screen::shapeOf(720, 720) != assist_screen::kSquare || assist_screen::shapeOf(480, 800) != assist_screen::kTall) {
    printf("FAIL: layout info\n");
    return 1;
  }
  for (int tall = 0; tall < 2; ++tall) {
    assist_screen::reset(screen, 1);
    const float w = tall ? 480 : 1024, h = tall ? 800 : 600;
    assist_screen::frame(screen, assist_screen::Inputs{0, 0, 0, 0, false, 50, w / 2, h / 2, w, h});
    const int m = assist_screen::dirty(screen, areas, 8);
    // Wide: one disc at x 10. Tall: the layout moves it to 60 and the tall-only disc shows at 120.
    const int want = tall ? 2 : 1;
    if (static_cast<int>(assist_screen::commandCount(screen)) != want || m < 1) { printf("FAIL: on/shape\n"); return 1; }
    const int32_t x1 = areas[0].x1;
    if ((tall && (x1 > 55 || areas[m - 1].x2 < 120)) || (!tall && (x1 > 5 || areas[0].x2 > 20))) {
      printf("FAIL: layout var (%d..%d)\n", static_cast<int>(x1), static_cast<int>(areas[m - 1].x2));
      return 1;
    }
  }
  // The copies built into the firmware (minified, without notes) load too.
  for (size_t i = 0; i < assist_screen_files::kFileCount; ++i) {
    const assist_screen_files::File& f = assist_screen_files::kFiles[i];
    if (!assist_screen::load(screen, f.text, f.length, error, sizeof(error))) {
      printf("FAIL: built-in copy of %s: %s\n", f.id, error);
      return 1;
    }
  }
  if (assist_screen_files::kFileCount != 9) { printf("FAIL: built-in copies\n"); return 1; }

  // Names, format version, text and history rules.
  refused(file("{\"type\": \"line\", \"let\": {\"v\": 1}, \"x1\": 0, \"y1\": 0, \"x2\": 1, \"y2\": 1}",
               ", \"vars\": {\"v\": 1}"), "already a var");
  refused("{\"format\": 2, \"id\": \"t\", \"items\": []}", "needs format 2");
  refused(file("{\"type\": \"text\", \"x\": 0, \"y\": 0, \"text\": \"Listening\"}"), "at most 8");
  refused(file("{\"type\": \"text\", \"x\": 0, \"y\": 0, \"text\": \"{t:100d}\"}"), "two digits");
  refused(file("{\"type\": \"disc\", \"x\": \"prev(a, 1)\", \"y\": 0, \"radius\": 5}",
               ", \"vars\": {\"a\": {\"init\": 0, \"next\": \"a + 1\"}}"), "prev()");
  refused(file("", ", \"vars\": {\"a\": {\"init\": 0, \"next\": \"a + 1\", \"record\": 1}}"), "'record' needs 'keep'");
  refused(file("{\"type\": \"line\", \"x1\": \"" + std::string(20000, '(') + "\", \"y1\": 0, \"x2\": 1, \"y2\": 1}"), "");

  // History: prev(var, k) is the value k changes ago; record picks the steps.
  if (!loadText(file("{\"type\": \"disc\", \"x\": 10, \"y\": 10, \"radius\": 5,"
                     " \"if\": \"frame == 6 && prev(c, 1) == 5 && prev(c, 3) == 3 && prev(c, 9) == 3 && prev(c, 0) == c"
                     " && prev(d, 1) == 5 && prev(d, 2) == 3\"}",
                     ", \"vars\": {\"c\": {\"init\": 0, \"next\": \"c + 1\", \"keep\": 3},"
                     " \"d\": {\"init\": 0, \"next\": \"d + 1\", \"keep\": 2, \"record\": \"mod(d, 2) == 1\"}}"))) {
    printf("FAIL: %s\n", error);
    return 1;
  }
  assist_screen::reset(screen, 1);
  for (int i = 0; i < 6; ++i) assist_screen::frame(screen, assist_screen::Inputs{i / 30.0f, 1 / 30.0f, 0, 0, false, 50, 50, 50, 100, 100});
  if (assist_screen::commandCount(screen) != 1) { printf("FAIL: history\n"); return 1; }

  // Functions such as sin weigh more than arithmetic in the frame budget.
  uint32_t cost[2] = {};
  for (int k = 0; k < 2; ++k) {
    if (!loadText(file(std::string("{\"type\": \"line\", \"x1\": \"") + (k ? "sin(t)" : "t + 1") +
                       "\", \"y1\": 0, \"x2\": 1, \"y2\": 1}"))) {
      printf("FAIL: %s\n", error);
      return 1;
    }
    cost[k] = assist_screen::info(screen).frame_ops;
  }
  if (cost[1] < cost[0] + 5) { printf("FAIL: weights %u %u\n", cost[0], cost[1]); return 1; }

  // Fill budget: shapes that would paint the screen many times over load
  // (each is legal) but a frame stops at the budget instead of taking seconds.
  const char* heavy[] = {
      "{\"type\": \"group\", \"repeat\": 4, \"items\": [{\"type\": \"disc\", \"repeat\": 256, \"x\": 240, \"y\": 240,"
      " \"radius\": 4000, \"alpha\": 0.5}]}",
      "{\"type\": \"arc\", \"repeat\": 256, \"glow\": true, \"x\": 240, \"y\": 240, \"radius\": 200, \"width\": 120}",
      "{\"type\": \"rect\", \"repeat\": 256, \"x1\": -9999, \"y1\": -9999, \"x2\": 9999, \"y2\": 9999}"};
  for (const char* items : heavy) {
    if (!loadText(file(items))) { printf("FAIL: %s\n", error); return 1; }
    assist_screen::reset(screen, 1);
    assist_screen::frame(screen, assist_screen::Inputs{0, 0, 0, 0, false, 80, 240, 216, 480, 480});
    if (assist_screen::commandCount(screen) > 40 || assist_screen::skippedShapes(screen) < 100) {
      printf("FAIL: fill budget (%u shapes, %u skipped)\n", assist_screen::commandCount(screen),
             assist_screen::skippedShapes(screen));
      return 1;
    }
  }
  puts("limits ok");
  return 0;
}
`;

// The firmware's built-in copies are generated from the files.
const generated = spawnSync(process.execPath, [path.join(root, 'tools/generate-assist-screens.mjs'), '--check'], {encoding: 'utf8'});
assert.equal(generated.status, 0, generated.stdout + generated.stderr);

const out = path.join(root, 'build/tests/assist-screen-files');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
const binary = path.join(out, 'test');
fs.writeFileSync(source, program);
// Address and undefined-behaviour checks where the compiler has them: the
// fuzz pass above must not read or write out of bounds.
const probe = spawnSync(host.cxx, ['-fsanitize=address,undefined,float-cast-overflow', '-x', 'c++', '-', '-o', path.join(out, 'probe')],
  {input: 'int main() { return 0; }', encoding: 'utf8'});
const sanitize = probe.status === 0 ? ['-fsanitize=address,undefined,float-cast-overflow', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer'] : [];
const build = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-O1', ...sanitize, '-I', lab, source,
  '-DHOMETILES_ASSIST_DATA_SCREENS=1', path.join(root, 'src/ui/assist/assist_screen.cpp'), path.join(root, 'src/ui/assist/assist_raster.cpp'), host.archive, '-lm', '-o', binary], {encoding: 'utf8'});
assert.equal(build.status, 0, build.stdout + build.stderr);
// One file per built-in style, in speaker::AssistScreenStyle order.
const ids = ['atom', 'reactor', 'dot_halo', 'voice_pills', 'sentinel', 'phosphor', 'light_disc', 'lens', 'iris'];
// Every file in screens/ loads and runs: the built-in styles first, then
// examples such as robot.json.
const extra = fs.readdirSync(path.join(lab, 'screens')).filter(f => f.endsWith('.json'))
  .map(f => f.replace('.json', '')).filter(id => !ids.includes(id)).sort();
const screens = [...ids, ...extra].map(id => path.join(lab, 'screens', `${id}.json`));
const run = spawnSync(binary, screens, {encoding: 'utf8', env: {...process.env, ASAN_OPTIONS: 'detect_leaks=0'}});
assert.equal(run.status, 0, run.stdout + run.stderr);
assert.ok(!/runtime error|AddressSanitizer/.test(run.stderr), run.stderr);
const lines = run.stdout.trim().split('\n');
assert.equal(lines.at(-1), 'limits ok');
for (const id of [...ids, ...extra]) {
  assert.ok(lines.some(line => line.startsWith(`${id} `)), `${id} ran`);
  assert.ok(lines.some(line => line.startsWith(`fuzz ${id} `)), `${id} fuzzed`);
}
// The files name the panel features they need like the style catalog does.
const needs = id => JSON.parse(fs.readFileSync(path.join(lab, 'screens', `${id}.json`), 'utf8')).needs ?? [];
// Sentinel and Phosphor bring their own square and tall layouts instead of
// needing a wide screen like the built-in styles.
for (const id of ['sentinel', 'phosphor']) {
  const layouts = JSON.parse(fs.readFileSync(path.join(lab, 'screens', `${id}.json`), 'utf8')).layouts ?? {};
  assert.deepEqual(Object.keys(layouts).sort(), ['square', 'tall'], `${id} layouts`);
  assert.deepEqual(needs(id), []);
}
assert.deepEqual(needs('light_disc'), ['tear_free']);
for (const id of ['atom', 'reactor', 'dot_halo', 'voice_pills', 'lens', 'iris']) assert.deepEqual(needs(id), []);
console.log(`assist screen files ok${sanitize.length ? ' (ASan/UBSan)' : ''}: ` +
  lines.filter(line => [...ids, ...extra].some(id => line.startsWith(`${id} `))).join(', '));
