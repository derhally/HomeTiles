// Host regression for the Assist screen's frame timing (assist_timing.h): the
// overlay measures every showing per screen id and phase, the Web Admin
// diagnostics page reads it over /api/assist-timing on panels with a speaker,
// and every language carries the card's strings at the same place in the
// schema. The module itself compiles on the host against stub headers and its
// JSON is checked for shape and arithmetic.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

// --- the overlay feeds the module ---
const overlay = read('src/ui/assist/assist_overlay.cpp');
assert.match(overlay, /#include "src\/ui\/assist\/assist_timing\.h"/);
assert.match(overlay, /assist_timing::attach\(\);\s*assist_timing::begin\(timing_id, g_data\);/);
assert.match(overlay, /if \(g_data\) timing_id = assist_screen::info\(g_screen\)\.id;/);
assert.match(overlay, /assist_timing::end\(\);/);
assert.match(overlay, /assist_timing::noteFrame\(micros\(\) - work_started\);/);
assert.match(overlay, /assist_timing::setPhase\(phase == AssistUiPhase::Answering\s*\? assist_timing::kAnswering/);

// --- the Web Admin reads it ---
const routes = read('src/web/server/web_admin.cpp');
assert.match(routes, /server\.on\("\/api\/assist-timing", HTTP_GET,\s*guarded\(\[this\]\(\) \{ this->handleAssistTiming\(\); \}\)\);/);
assert.match(routes, /server\.on\("\/api\/assist-timing\/reset", HTTP_POST,\s*guarded\(\[this\]\(\) \{ this->handleAssistTimingReset\(\); \}\)\);/);
const handlers = read('src/web/server/handlers/web_admin_diagnostics.cpp');
assert.match(handlers, /void WebAdminServer::handleAssistTiming\(\) \{[^}]*assist_timing::toJson\(json\);/s);
assert.match(handlers, /void WebAdminServer::handleAssistTimingReset\(\) \{[^}]*assist_timing::reset\(\);/s);
const html = read('src/web/server/render/web_admin_html.cpp');
// The card sits in the diagnostics page, on speaker panels only, with every
// header from the central translations.
const card = html.slice(html.indexOf('if (speaker::supported()) {'), html.indexOf('id="assist_timing_rows"'));
assert.ok(card.includes('tr.assist_timing_title'), 'card title translated');
for (const f of ['screen', 'phase', 'frames', 'draw', 'copy', 'pixels', 'engine', 'interval', 'late']) {
  assert.ok(card.includes(`tr.assist_timing_${f}`), `column ${f} translated`);
}
assert.match(html, /<tr><td colspan="9">\)html";\s*html \+= tr\.assist_timing_empty;/);
const scripts = read('src/web/server/render/web_admin_scripts.cpp');
for (const [key, field] of [['assistTimingEmpty', 'assist_timing_empty'], ['assistListening', 'assist_listening'],
  ['assistThinking', 'assist_thinking'], ['assistAnswering', 'assist_answering']]) {
  assert.ok(scripts.includes(`appendJsEntry("${key}", tr.${field});`), `${key} exported to the page`);
}
const js = read('src/web/admin/settings/diagnostics.js');
for (const fn of ['loadAssistTiming', 'resetAssistTiming', 'renderAssistTiming']) {
  assert.match(js, new RegExp(`function ${fn}\\(`), `${fn} defined`);
}
assert.match(js, /fetch\('\/api\/assist-timing\?ts=' \+ Date\.now\(\)\)/);
assert.match(js, /fetch\('\/api\/assist-timing\/reset', \{ method: 'POST' \}\)/);
// Screen ids and texts reach the page escaped.
assert.match(js, /escapeHtml\(screen\.id \|\| ''\)/);
assert.match(js, /escapeHtml\(assistTimingPhaseName\(p\.phase\)\)/);
// The page loads the table when the diagnostics page shows.
assert.match(read('src/web/admin/settings/pages.js'), /activeSettingsPage === 'diagnostics' && typeof loadAssistTiming === 'function' && !assistTimingLoaded/);
assert.ok(read('src/web/assets/admin.js').includes('function loadAssistTiming('), 'the assembled bundle carries it');

// --- translations: one block, every language ---
const header = read('src/core/i18n/i18n.h');
const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const schema = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(m => m[1]);
const fields = ['assist_timing_title', 'assist_timing_note', 'assist_timing_refresh', 'assist_timing_reset',
  'assist_timing_screen', 'assist_timing_phase', 'assist_timing_frames', 'assist_timing_draw', 'assist_timing_copy',
  'assist_timing_pixels', 'assist_timing_engine', 'assist_timing_interval', 'assist_timing_late', 'assist_timing_empty'];
const at = schema.indexOf(fields[0]);
assert.deepEqual(schema.slice(at, at + fields.length), fields, 'the timing strings sit together');
const strings = read('src/core/i18n/i18n.cpp');
const english = [];
for (const table of ['kStringsDe', 'kStringsEn', 'kStringsFr', 'kStringsPl', 'kStringsCs', 'kStringsEs', 'kStringsSk']) {
  const body = strings.slice(strings.indexOf(`static const Strings ${table} = {`));
  const values = [...body.slice(0, body.indexOf('};') + 2).matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1]);
  assert.equal(values.length, schema.length, `${table} has one value per field`);
  const block = fields.map(f => values[schema.indexOf(f)]);
  for (const value of block) assert.ok(value, `${table} has every timing string`);
  if (table === 'kStringsEn') english.push(...block);
  else assert.notDeepEqual(block, english, `${table} is translated`);
}
assert.equal(english[0], 'Assist screen drawing');
assert.equal(english[13], 'No frames measured yet');

// --- the module on the host: slots, phases, p95, JSON ---
const cxx = [process.env.CXX, 'clang++', 'g++'].filter(Boolean).find(c => spawnSync(c, ['--version']).status === 0);
if (!cxx) {
  console.log('SKIP: assist timing host check needs a C++ compiler');
  process.exit(0);
}
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'assist-timing-'));
const stubs = path.join(work, 'stubs');
fs.mkdirSync(stubs);
// Arduino, LVGL and the heap: just enough for the module.
fs.writeFileSync(path.join(stubs, 'Arduino.h'), `#pragma once
#include <stdint.h>
extern uint32_t g_now_us;
inline uint32_t micros() { return g_now_us; }
`);
fs.writeFileSync(path.join(stubs, 'WString.h'), `#pragma once
#include <string>
#include <stdint.h>
struct String : std::string {
  String() {}
  String(const char* s) : std::string(s) {}
  String(uint32_t v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  String(int v) : std::string(std::to_string(v)) {}
  void reserve(size_t n) { std::string::reserve(n); }
  String& operator+=(const char* s) { std::string::operator+=(s); return *this; }
  String& operator+=(char c) { std::string::operator+=(c); return *this; }
  String& operator+=(const String& s) { std::string::operator+=(s); return *this; }
};
`);
fs.writeFileSync(path.join(stubs, 'esp_heap_caps.h'), `#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
`);
fs.writeFileSync(path.join(stubs, 'lvgl.h'), `#pragma once
#include <stdint.h>
#include <stdlib.h>
typedef struct { int32_t x1, y1, x2, y2; } lv_area_t;
inline uint32_t lv_area_get_size(const lv_area_t* a) { return (uint32_t)(a->x2 - a->x1 + 1) * (uint32_t)(a->y2 - a->y1 + 1); }
typedef enum { LV_EVENT_ALL = 0, LV_EVENT_REFR_START = 1, LV_EVENT_REFR_READY, LV_EVENT_FLUSH_START, LV_EVENT_FLUSH_FINISH, LV_EVENT_OTHER } lv_event_code_t;
struct lv_event_t { lv_event_code_t code; void* param; };
typedef void (*lv_event_cb_t)(lv_event_t*);
struct lv_display_t { lv_event_cb_t cb; };
extern lv_display_t g_display;
inline lv_display_t* lv_display_get_default() { return &g_display; }
inline void lv_display_add_event_cb(lv_display_t* d, lv_event_cb_t cb, lv_event_code_t, void*) { d->cb = cb; }
inline lv_event_code_t lv_event_get_code(lv_event_t* e) { return e->code; }
inline void* lv_event_get_param(lv_event_t* e) { return e->param; }
`);
const main = path.join(work, 'main.cpp');
fs.writeFileSync(main, `#include "src/ui/assist/assist_timing.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
uint32_t g_now_us = 1000;
lv_display_t g_display;
static void send(lv_event_code_t code, void* param = nullptr) { lv_event_t e{code, param}; g_display.cb(&e); }
// One refresh that draws px pixels and takes draw_us, copy_us of it in the flush.
static void refresh(uint32_t draw_us, uint32_t copy_us, uint32_t px) {
  send(LV_EVENT_REFR_START);
  g_now_us += draw_us - copy_us;
  lv_area_t area{0, 0, (int32_t)px - 1, 0};
  send(LV_EVENT_FLUSH_START, &area);
  g_now_us += copy_us;
  send(LV_EVENT_FLUSH_FINISH);
  send(LV_EVENT_REFR_READY);
}
int main() {
  assist_timing::attach();
  assist_timing::attach();
  refresh(5000, 1000, 100);  // nothing shown: not counted
  assist_timing::begin("atom", true);
  refresh(50000, 20000, 614400);  // the opening frame
  // Ticks 50 ms apart: the tick, the frame's draw, then the rest of the wait.
  for (int i = 0; i < 20; ++i) {
    const uint32_t draw = i == 19 ? 40000 : 10000 + i * 100;
    assist_timing::noteFrame(700);
    refresh(draw, 3000, 200000);
    g_now_us += 50000 - draw;
  }
  send(LV_EVENT_REFR_START);  // a refresh that draws nothing
  send(LV_EVENT_REFR_READY);
  assist_timing::setPhase(assist_timing::kThinking);
  assist_timing::noteFrame(900);
  refresh(8000, 2000, 100000);
  assist_timing::end();
  refresh(70000, 1000, 1);  // hidden: not counted
  assist_timing::begin("sentinel", true);
  refresh(60000, 10000, 614400);
  assist_timing::noteFrame(2000);
  refresh(20000, 5000, 300000);
  assist_timing::end();
  String json;
  assist_timing::toJson(json);
  printf("%s\\n", json.c_str());
  assist_timing::reset();
  String empty;
  assist_timing::toJson(empty);
  printf("%s\\n", empty.c_str());
  return 0;
}
`);
const exe = path.join(work, 'timing');
const build = spawnSync(cxx, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', stubs, '-I', root, main,
  path.join(root, 'src/ui/assist/assist_timing.cpp'), '-o', exe], {encoding: 'utf8'});
assert.equal(build.status, 0, build.stdout + build.stderr);
const run = spawnSync(exe, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stderr);
const [full, empty] = run.stdout.trim().split('\n').map(line => JSON.parse(line));
fs.rmSync(work, {recursive: true, force: true});

assert.equal(full.screens.length, 2);
const [atom, sentinel] = full.screens;
assert.equal(atom.id, 'atom');
assert.equal(atom.file, true);
assert.equal(atom.open_us, 50000, 'the first refresh is the opening, kept apart');
const listening = atom.phases[0];
assert.equal(listening.phase, 'listening');
assert.equal(listening.frames, 20);
// 19 frames of 10000 + i * 100 (i = 0..18) and one of 40000.
assert.equal(listening.draw_us, Math.floor((19 * 10000 + 100 * 171 + 40000) / 20));
assert.equal(listening.max_us, 40000);
assert.equal(listening.p95_us, 12000, 'p95 is the 1 ms bucket holding the 19th of 20 frames');
assert.equal(listening.copy_us, 3000);
assert.equal(listening.px, 200000);
assert.equal(listening.late, 1);
assert.equal(listening.ticks, 20);
assert.equal(listening.work_us, 700);
assert.equal(listening.gap_us, 50000);
assert.equal(listening.gap_max_us, 50000);
const thinking = atom.phases[1];
assert.equal(thinking.frames, 1);
assert.equal(thinking.draw_us, 8000);
assert.equal(thinking.ticks, 1);
assert.equal(thinking.gap_us, 0, 'no interval across a phase change');
assert.equal(atom.phases[2].frames, 0);
assert.equal(sentinel.id, 'sentinel');
assert.equal(sentinel.open_us, 60000);
assert.equal(sentinel.phases[0].frames, 1);
assert.equal(sentinel.phases[0].draw_us, 20000);
assert.deepEqual(empty, {screens: []});
console.log('assist timing: ok');
