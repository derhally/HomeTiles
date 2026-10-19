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

// The Assist screen strings and the Settings > Audio Assist group's strings
// are translated in every locale, read by their place in the schema.
const fields = ['assist_listening', 'assist_thinking', 'assist_answering'];
const styleFields = ['settings_assist', 'settings_assist_style', 'assist_style_atom', 'assist_style_reactor',
  'assist_style_dot_halo', 'assist_style_voice_pills', 'assist_style_sentinel', 'assist_style_phosphor',
  'assist_style_light_disc', 'assist_style_lens', 'assist_style_iris', 'settings_assist_preview'];
const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const schema = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(m => m[1]);
for (const group of [fields, styleFields]) {
  const at = schema.indexOf(group[0]);
  assert.deepEqual(schema.slice(at, at + group.length), group, `${group[0]}.. sit together`);
}
const english = [];
for (const table of ['kStringsDe', 'kStringsEn', 'kStringsFr', 'kStringsPl', 'kStringsCs', 'kStringsEs', 'kStringsSk']) {
  const body = strings.slice(strings.indexOf(`static const Strings ${table} = {`));
  const values = [...body.slice(0, body.indexOf('};') + 2).matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1]);
  assert.equal(values.length, schema.length, `${table} has one value per field`);
  const screen = fields.map(f => values[schema.indexOf(f)]);
  const style = styleFields.map(f => values[schema.indexOf(f)]);
  for (const value of screen) assert.match(value, /…$/, `${table} has every Assist screen string`);
  for (const value of style) assert.ok(value, `${table} has every Assist style string`);
  if (table === 'kStringsEn') english.push(...screen);
  else assert.notDeepEqual(screen, english, `${table} is translated`);
}
assert.deepEqual(english, ['Listening…', 'Thinking…', 'Answering…']);
// The overlay shows them through the central schema.
for (const f of fields) assert.match(overlay, new RegExp(`tr\\(\\)\\.${f}`));
// Settings > Audio: the model names the styles in their stored order, saves a
// pick and previews; the page shows them as a dropdown row and a play row.
const settings = read('src/ui/tabs/settings/tab_settings.cpp');
for (const f of styleFields.slice(2, 11)) assert.match(settings, new RegExp(`return s\\.${f};`));
// The list holds only the styles this panel offers; an index is a position in it.
assert.match(settings, /assist_style_catalog::styleAt\(index, offered\)\)\);/);
assert.match(settings, /return assist_style_catalog::count\(speaker::availableAssistScreenStyles\(\)\);/);
assert.match(settings, /void assist_preview\(\) \{ assist_overlay::preview\(\); \}/);
const page = read('src/ui/tabs/settings/settings_screen.cpp');
assert.match(page, /settings_parts::section\(page, s\.settings_assist, false\);/);
assert.match(page, /settings_parts::row\(assist, "palette-outline", s\.settings_assist_style,/);
assert.match(page, /settings_parts::row\(assist, "animation-play-outline", s\.settings_assist_preview\);/);
assert.match(page, /void on_preview_clicked\(lv_event_t\*\) \{ settings_model::assist_preview\(\); \}/);

// Styles: stored by number (append only), nine of them, one module each.
assert.match(speakerHeader, /enum class AssistScreenStyle : uint8_t \{\s*Atom = 0,\s*Reactor = 1,\s*DotHalo = 2,\s*VoicePills = 3,\s*Sentinel = 4,\s*Phosphor = 5,\s*LightDisc = 6,\s*Lens = 7,\s*Iris = 8\s*\};/);
assert.match(speakerHeader, /kAssistScreenStyleCount = 9;/);
const contract = read('src/audio/assist_contract.h');
assert.match(contract, /"atom",\s*"reactor",\s*"dot_halo",\s*"voice_pills",\s*"sentinel",\s*"phosphor",\s*"light_disc",\s*"lens",\s*"iris"/);
assert.match(speaker, /g_screen_style\.store\(style < kAssistScreenStyleCount \? style : 0\);/);
assert.match(speaker, /prefs\.putUChar\(kPrefsScreenStyleKey, g_screen_style\.load\(\)\);/);
const styles = read('src/ui/assist/assist_styles.cpp');
const lines = read('src/ui/assist/assist_styles_lines.cpp');
for (const ns of ['atom', 'reactor', 'halo', 'pills']) {
  assert.match(styles, new RegExp(`namespace ${ns} \\{`));
  assert.match(styles, new RegExp(`${ns}::create\\(parent\\)`));
  assert.match(styles, new RegExp(`${ns}::frame\\(f\\)`));
}
for (const ns of ['sentinel', 'phosphor', 'disc', 'lens', 'iris']) {
  assert.match(lines, new RegExp(`namespace ${ns} \\{`));
  assert.match(styles, new RegExp(`${ns}::create\\(parent\\)`));
  assert.match(styles, new RegExp(`${ns}::frame\\(f\\)`));
}
// The line styles paint from a draw callback and invalidate only their box.
for (const ns of ['sentinel', 'phosphor', 'iris']) {
  const body = lines.slice(lines.indexOf(`namespace ${ns} {`), lines.indexOf(`}  // namespace ${ns}`));
  assert.match(body, /createPainter\(parent, onDraw/);
  assert.match(body, /invalidateBox\(/);
}
// The label follows the style's palette through the central hook.
assert.match(overlay, /lv_obj_set_style_text_color\(g_label, lv_color_hex\(assist_styles::labelColor\(style\)\), 0\);/);
// Only the chosen style's objects exist.
assert.match(overlay, /lv_obj_clean\(g_stage\);\s*assist_styles::create\(style, g_stage\);/);

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
for (const [source, ns] of [[styles, 'atom'], [styles, 'reactor'], [styles, 'halo'], [styles, 'pills'],
  [lines, 'sentinel'], [lines, 'phosphor'], [lines, 'disc'], [lines, 'lens'], [lines, 'iris']]) {
  const body = source.slice(source.indexOf(`namespace ${ns} {`));
  const styleFrame = body.slice(body.indexOf('void frame('), body.indexOf(`}  // namespace ${ns}`));
  const code = styleFrame.replace(/\/\/[^\n]*/g, '');
  assert.doesNotMatch(code, /_create\(|malloc|\bnew\b/, `${ns} allocates nothing per frame`);
  // Label text changes reuse static buffers (no label realloc per frame).
  assert.doesNotMatch(code, /lv_label_set_text\(|lv_label_set_text_fmt\(/, `${ns} sets only static label text`);
}
assert.doesNotMatch(overlay + styles + lines, /lv_obj_set_style_opa\(|LV_BLEND_MODE_ADDITIVE/);
console.log('Assist overlay checks passed.');
