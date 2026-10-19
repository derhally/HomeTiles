// Host regression for the Assist screen styles a panel offers
// (src/ui/assist/assist_style_catalog.h): which styles each screen shape and
// the tear-free capability offer, the fallback for a stored style the panel
// does not offer, the Settings list positions, and the status payload's
// "screen_styles" list the Bridge's select takes its options from. The
// overlay's wiring (offerStyles, the speaker's refusal and fallback) is
// checked in the sources.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const overlay = read('src/ui/assist/assist_overlay.cpp');
const speaker = read('src/audio/speaker.cpp');
const styles = read('src/ui/assist/assist_styles.cpp');
// The overlay works the offer out from the shown screen and hands it over.
assert.match(overlay, /assist_style_catalog::available\(assist_style_catalog::features\(w, h, g_tear_free\)\)/);
assert.match(overlay, /speaker::setAvailableAssistScreenStyles\(offered\);/);
assert.match(overlay, /Device::kCapabilities\.tear_free_ui && w == Device::kScreenWidth && h == Device::kScreenHeight/);
assert.match(overlay, /assist_styles::baseRadius\(g_style,/);
// The speaker shows the effective style, refuses styles it does not offer and
// publishes only the offered ones.
assert.match(speaker, /assist_style_catalog::effective\(g_screen_style\.load\(\), g_screen_styles\.load\(\)\)/);
assert.match(speaker, /refused: not offered on this panel/);
assert.match(speaker, /fields\.available_screen_styles = g_screen_styles\.load\(\);/);
// Only the 7B has the tear-free path.
const profiles = fs.readdirSync(path.join(root, 'src/devices'))
  .map(d => path.join('src/devices', d, 'profile.h')).filter(p => fs.existsSync(path.join(root, p)));
const tearFree = profiles.filter(p => /Capabilities\{[^}]*kBuiltinCamera, true\}/.test(read(p)));
assert.deepEqual(tearFree, ['src/devices/waveshare_touch_lcd_7b/profile.h']);
assert.match(styles, /float baseRadius\(AssistScreenStyle style, float w, float h\)/);

function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}
const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: assist style catalog test needs a host C++ compiler');
  process.exit(0);
}

const program = String.raw`
#include "src/audio/assist_contract.h"
#include "src/ui/assist/assist_style_catalog.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace c = assist_style_catalog;
using assist_contract::kScreenStyleIds;

static uint32_t offered(int w, int h, bool tear_free) { return c::available(c::features(w, h, tear_free)); }
static std::string ids(uint32_t mask) {
  std::string out;
  for (uint8_t i = 0; i < c::kStyleCount; ++i) {
    if (!(mask & (1u << i))) continue;
    if (!out.empty()) out += ',';
    out += kScreenStyleIds[i];
  }
  return out;
}

int main() {
  static_assert(c::kStyleCount == assist_contract::kScreenStyleCount, "one catalog line per style id");
  // 7B landscape: every style.
  assert(ids(offered(1024, 600, true)) == "atom,reactor,dot_halo,voice_pills,sentinel,phosphor,light_disc,lens,iris");
  // Landscape without the tear-free path (8-inch, 10.1, 4.3, Tab5 and V2 landscape): no Light Disc.
  assert(ids(offered(1280, 800, false)) == "atom,reactor,dot_halo,voice_pills,sentinel,phosphor,lens,iris");
  assert(ids(offered(800, 480, false)) == "atom,reactor,dot_halo,voice_pills,sentinel,phosphor,lens,iris");
  // Square (4B) and upright screens: no Sentinel, Phosphor or Light Disc.
  assert(ids(offered(720, 720, false)) == "atom,reactor,dot_halo,voice_pills,lens,iris");
  assert(ids(offered(480, 800, false)) == "atom,reactor,dot_halo,voice_pills,lens,iris");
  assert(ids(offered(720, 1280, false)) == "atom,reactor,dot_halo,voice_pills,lens,iris");
  // A board can take styles off; Atom stays.
  assert(c::available(c::features(1024, 600, true), 0x1FFu) == 1u);
  assert(ids(c::available(c::features(1024, 600, true), 1u << 5)) ==
         "atom,reactor,dot_halo,voice_pills,sentinel,light_disc,lens,iris");

  // A stored style the panel does not offer shows as the first offered one.
  const uint32_t square = offered(720, 720, false);
  assert(c::effective(5, square) == 0);
  assert(c::effective(7, square) == 7);
  assert(c::effective(200, square) == 0);
  // Settings positions: lens is the fifth offered on a square screen.
  assert(c::count(square) == 6);
  assert(c::styleAt(4, square) == 7 && c::positionOf(7, square) == 4);
  for (uint8_t p = 0; p < c::count(square); ++p) assert(c::positionOf(c::styleAt(p, square), square) == p);

  // The status lists only the offered styles (the Bridge's select options).
  assist_contract::StatusFields fields;
  fields.screen_style_state = true;
  fields.screen_style = 7;
  fields.available_screen_styles = square;
  char json[512];
  assert(assist_contract::buildStatusJson(json, sizeof(json), fields) > 0);
  assert(strstr(json, "\"screen_style\":\"lens\",\"screen_styles\":[\"atom\",\"reactor\",\"dot_halo\",\"voice_pills\",\"lens\",\"iris\"]"));
  // Every style by default (older callers).
  assist_contract::StatusFields all;
  all.screen_style_state = true;
  assert(assist_contract::buildStatusJson(json, sizeof(json), all) > 0);
  assert(strstr(json, "\"light_disc\",\"lens\",\"iris\"]"));
  std::puts("assist style catalog ok");
  return 0;
}
`;

const out = path.join(root, 'build/tests/assist-style-catalog');
fs.mkdirSync(out, {recursive: true});
const cpp = path.join(out, 'catalog.cpp');
const exe = path.join(out, process.platform === 'win32' ? 'catalog.exe' : 'catalog');
fs.writeFileSync(cpp, program);
const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root, cpp, '-o', exe], {encoding: 'utf8'});
assert.equal(build.status, 0, `catalog test did not compile:\n${build.stdout}${build.stderr}`);
const run = spawnSync(exe, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `catalog test failed:\n${run.stdout}${run.stderr}`);
console.log(run.stdout.trim());
