// Settings > Display & Sound on panels with a speaker: upstream's Sound group
// (volume, microphone switch) also carries the microphone sensitivity, and
// an Assist group (screen style dropdown, Preview) follows where the
// microphone answered. There is no separate Audio category.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const header = readRepoFile('src/core/i18n/i18n.h');
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const rawScreen = readRepoFile('src/ui/tabs/settings/settings_screen.cpp');
const screen = maskCpp(rawScreen);
const modelHeader = readRepoFile('src/ui/tabs/settings/settings_model.h');
const model = maskCpp(readRepoFile('src/ui/tabs/settings/tab_settings.cpp'));
const style = readRepoFile('src/ui/tabs/settings/settings_style.h');
const preview = readRepoFile('tools/settings-preview.mjs');

// --- Translations: every language has the rows' texts ----------------------------------
const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const fields = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(match => match[1]);
for (const gone of ['audio_label', 'settings_speaker', 'settings_mic_off']) {
  assert.ok(!fields.includes(gone), `${gone} left the Strings struct with the Audio page`);
}
const keys = ['settings_mic_sensitivity', 'settings_assist', 'settings_assist_style', 'settings_assist_preview'];
const tables = [...new Set([...i18n.matchAll(/\{&(kStrings\w+), &kLocale\w+\}/g)].map(match => match[1]))];
assert.ok(tables.length >= 7, 'every registered language');
for (const table of tables) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
    .map(match => match[1]);
  assert.equal(values.length, fields.length, `${table} has one value per field`);
  for (const key of keys) {
    const index = fields.indexOf(key);
    assert.ok(index >= 0, key);
    assert.ok(values[index].length > 0, `${table} ${key}`);
  }
}
assert.equal(values(table => table, 'kStringsEn', 'settings_mic_sensitivity'), 'Sensitivity');
assert.equal(values(table => table, 'kStringsDe', 'settings_mic_sensitivity'), 'Empfindlichkeit');
function values(_, table, key) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const list = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)].map(m => m[1]);
  return list[fields.indexOf(key)];
}

// --- No Audio category ------------------------------------------------------------------
assert.match(screen, /enum class Category : uint8_t \{ Display = 0, Wifi, Localization, System \};/);
assert.doesNotMatch(screen, /Category::Audio|build_audio_page|kAudioColor/);
assert.doesNotMatch(style, /kAudioColor/);
for (const gone of ['audio_available', 'AudioValues', 'mic_selected(']) {
  assert.ok(!modelHeader.includes(gone), `settings_model.h: ${gone} is gone`);
}

// --- Display page: sensitivity in the Sound group, then the Assist group ----------------
const page = rawScreen.slice(rawScreen.indexOf('void build_display_page('), rawScreen.indexOf('// ---------- Localization page'));
const sound = page.slice(page.indexOf('if (sound.microphone) {'), page.indexOf('settings_parts::section(page, s.settings_assist'));
assert.match(sound, /display_slider_row\(audio, "microphone-settings", s\.settings_mic_sensitivity, width, 0, gain\.max, gain\.step,\s*on_mic_gain, &g_mic_gain_value\);/,
  'the sensitivity slider follows the microphone switch in the Sound group');
assert.match(page, /if \(sound\.speaker && sound\.microphone\) \{\s*settings_parts::section\(page, s\.settings_assist, false\);/,
  'the Assist group shows only where the microphone answered');
assert.match(page, /settings_parts::row\(assist, "palette-outline", s\.settings_assist_style,/);
assert.match(page, /settings_parts::row\(assist, "animation-play-outline", s\.settings_assist_preview\);/);
assert.ok(page.indexOf('s.settings_assist,') < page.indexOf('s.settings_screensaver'), 'Assist sits before Screensaver');
// Every slider on the page starts at the same x: the sensitivity name counts.
assert.match(screen, /for \(const char\* text : \{s\.settings_brightness, s\.settings_volume, s\.settings_mic_sensitivity, s\.settings_sleep,/);
// The refs belong to the Display page and are cleared with it; an open style list closes.
assert.match(screen, /void clear_display_refs\(\) \{\s*if \(g_style_row\) settings_parts::close_options\(\);\s*g_mic_gain_value = nullptr;/);

// --- Model -------------------------------------------------------------------------------
assert.match(modelHeader, /struct MicGain \{\s*int step;[^\n]*\n\s*int max;\s*\};\s*MicGain mic_gain\(\);/);
assert.match(model, /MicGain mic_gain\(\) \{ return MicGain\{speaker::micGainStep\(\), speaker::kMaxMicGainStep\}; \}/);
assert.match(model, /void mic_gain_changed\(int step, bool final\) \{\s*if \(final\) speaker::setMicGainStep/);

// --- Host preview has the same calls -----------------------------------------------------
assert.match(preview, /MicGain mic_gain\(\) \{ return MicGain\{10, 14\}; \}/);
assert.doesNotMatch(preview, /audio_available|AudioValues|--page=audio/);
