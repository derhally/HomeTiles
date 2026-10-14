import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('../../..', import.meta.url)));
const read = path => readFileSync(resolve(root, path), 'utf8');
const header = read('src/core/i18n/i18n.h');
const strings = read('src/core/i18n/i18n.cpp');
const speakerHeader = read('src/audio/speaker.h');
const speaker = read('src/audio/speaker.cpp');
const overlay = read('src/ui/assist/assist_overlay.cpp');
const sketch = read('HomeTiles.ino');

// The three Assist screen strings close the schema and are translated in
// every locale.
const fields = ['assist_listening', 'assist_thinking', 'assist_answering'];
assert.match(header, new RegExp(fields.map(f => `const char\\* ${f};`).join('\\s*') + '\\s*};'));
const english = [];
for (const table of ['kStringsDe', 'kStringsEn', 'kStringsFr', 'kStringsPl', 'kStringsCs', 'kStringsEs', 'kStringsSk']) {
  const body = strings.slice(strings.indexOf(`static const Strings ${table} = {`));
  const values = [...body.slice(0, body.indexOf('};') + 2).matchAll(/"((?:[^"\\]|\\.)*)"/g)]
    .map(m => m[1]).slice(-3);
  assert.equal(values.length, 3, table);
  for (const value of values) assert.match(value, /…$/, `${table} has every Assist screen string`);
  if (table === 'kStringsEn') english.push(...values);
  else assert.notDeepEqual(values, english, `${table} is translated`);
}
assert.deepEqual(english, ['Listening…', 'Thinking…', 'Answering…']);
// The overlay shows them through the central schema.
for (const f of fields) assert.match(overlay, new RegExp(`tr\\(\\)\\.${f}`));

// Phase contract: any wake word or Assist start listens; a normal end of the
// capture thinks; the answer clip answers; its end or a failure is idle.
assert.match(speakerHeader, /enum class AssistUiPhase : uint8_t \{ Idle, Listening, Thinking, Answering \};/);
assert.match(speaker, /setAssistPublic\(assist::PublicState::Wake, assist::ErrorCode::None\);\s*setUiPhase\(AssistUiPhase::Listening\);/);
assert.match(speaker, /if \(!handoff\) \{\s*g_mic_level\.store\(0\);\s*setUiPhase\(AssistUiPhase::Listening\);/);
assert.match(speaker, /answer_expected \? AssistUiPhase::Thinking : AssistUiPhase::Idle/);
assert.match(speaker, /advanceUiPhase\(AssistUiPhase::Thinking, AssistUiPhase::Answering\);/);
assert.match(speaker, /advanceUiPhase\(AssistUiPhase::Answering, AssistUiPhase::Idle\);/);
assert.match(speaker, /ui_phase_age >= kAssistThinkingTimeoutMs/);
// Every abandoned wake word handoff ends the listening screen.
const handoff = speaker.slice(speaker.indexOf('bool handleWakeDetection('), speaker.indexOf('uint32_t wakeListen() {'));
assert.equal((handoff.match(/advanceUiPhase\(AssistUiPhase::Listening, AssistUiPhase::Idle\);/g) || []).length, 3);

// Phase transitions in plain JS, mirroring advanceUiPhase's compare-exchange.
const run = events => {
  let phase = 'Idle';
  const advance = (from, to) => { if (phase === from) phase = to; };
  for (const e of events) {
    if (e === 'wake' || e === 'start') phase = 'Listening';
    else if (e === 'handoff-start') { /* keeps the wake word's phase */ }
    else if (e === 'stopped') advance('Listening', 'Thinking');
    else if (e === 'error') advance('Listening', 'Idle');
    else if (e === 'play') advance('Thinking', 'Answering');
    else if (e === 'played') advance('Answering', 'Idle');
    else if (e === 'tap') phase = 'Idle';
  }
  return phase;
};
assert.equal(run(['wake', 'handoff-start', 'stopped']), 'Thinking');
assert.equal(run(['wake', 'start', 'stopped', 'play']), 'Answering');
assert.equal(run(['wake', 'start', 'stopped', 'play', 'played']), 'Idle');
assert.equal(run(['start', 'error']), 'Idle');
assert.equal(run(['play']), 'Idle', 'music without a conversation shows nothing');
assert.equal(run(['wake', 'tap', 'handoff-start', 'stopped', 'play']), 'Idle', 'a dismissed screen stays hidden');
assert.equal(run(['wake', 'tap', 'start']), 'Listening', 'a new conversation shows it again');

// Loop wiring: the active loop services the screen, the sleep loop wakes for it.
assert.match(sketch, /speaker::service\(\);\s*assist_overlay::service\(\);/);
assert.match(sketch, /if \(assist_overlay::serviceSleep\(\)\) \{\s*was_asleep = false;\s*return;\s*\}/);
// No per-frame allocation and no layer-forcing opacity on the animated parts.
const frame = overlay.slice(overlay.indexOf('void onFrame('), overlay.indexOf('void onTap('));
assert.doesNotMatch(frame, /lv_obj_create|lv_.*_create\(|malloc|new /);
assert.doesNotMatch(overlay, /lv_obj_set_style_opa\(|LV_BLEND_MODE_ADDITIVE/);
console.log('Assist overlay checks passed.');
