import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('../../..', import.meta.url)));
const read = path => readFileSync(resolve(root, path), 'utf8');
const header = read('src/core/i18n/i18n.h');
const strings = read('src/core/i18n/i18n.cpp');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');
const speakerHeader = read('src/audio/speaker.h');

// Every locale ends with the seven Audio strings, translated.
const fields = ['audio_label', 'settings_tile_desc_audio', 'audio_volume_label',
  'audio_mic_gain_label', 'audio_mic_on_btn', 'audio_mic_off_btn', 'audio_formats_info'];
const struct = header.slice(header.indexOf('struct Strings {'));
assert.match(struct.slice(0, struct.indexOf('};')),
  new RegExp(fields.map(f => `const char\\* ${f};`).join('\\s*') + '\\s*$'));
const english = {};
for (const table of ['kStringsDe', 'kStringsEn', 'kStringsFr', 'kStringsPl']) {
  const body = strings.slice(strings.indexOf(`static const Strings ${table} = {`));
  const values = [...body.slice(0, body.indexOf('};') + 2).matchAll(/"((?:[^"\\]|\\.)*)"/g)]
    .map(m => m[1]).slice(-fields.length);
  assert.equal(values.length, fields.length, table);
  for (const value of values) assert.ok(value.length > 0, `${table} has every Audio string`);
  if (table === 'kStringsEn') fields.forEach((f, i) => { english[f] = values[i]; });
  else assert.notDeepEqual(values.slice(1, 6), Object.values(english).slice(1, 6), `${table} is translated`);
}
assert.equal(english.audio_mic_gain_label, 'Mic sensitivity');
assert.match(english.audio_formats_info, /MP3 and 16-bit WAV/);

// Two equal columns of two-cell tiles only on speaker panels with room.
assert.match(settings, /if \(speaker::supported\(\) && GRID_COLS >= 1 \+ 2 \* kSettingsTwoColumnSpanW && GRID_ROWS >= 3\) \{/);
const twoColumns = settings.slice(settings.indexOf('constexpr uint8_t left = 1;'), settings.indexOf('  } else {', settings.indexOf('constexpr uint8_t left = 1;')));
assert.equal((twoColumns.match(/create_settings_menu_tile\(/g) || []).length, 5);
assert.equal((twoColumns.match(/kSettingsTwoColumnSpanW\);/g) || []).length, 5, 'every tile has the same width');
assert.match(twoColumns, /SettingsPopupKind::Audio/);

// The popup: volume applies on release, the gain slider covers every ES7210
// step, the mic controls only where the microphone answered.
assert.match(settings, /if \(code == LV_EVENT_RELEASED \|\| code == LV_EVENT_PRESS_LOST\) \{\s*speaker::setVolume\(static_cast<float>\(pct\) \/ 100\.0f\);/);
assert.match(settings, /lv_slider_set_range\(gain_slider, 0, speaker::kMaxMicGainStep\);/);
assert.match(settings, /if \(speaker::micCapability\(\)\) \{\s*audio_mic_gain_row = create_display_control_row\(form\);/);
assert.match(settings, /speaker::setMicEnabled\(!speaker::micEnabled\(\), "panel"\);/);
assert.match(settings, /audio_mic_btn = nullptr;/, 'reset_popup_refs forgets the popup controls');

// Gain steps: 0..30 dB by 3, then 33, 34.5, 36, 37.5 dB.
const tenths = step => (step <= 10 ? step * 30 : 330 + (step - 11) * 15);
assert.deepEqual([0, 1, 10, 11, 12, 13, 14].map(tenths), [0, 30, 300, 330, 345, 360, 375]);
assert.match(speakerHeader, /return step <= 10 \? static_cast<uint16_t>\(step \* 30\)\s*: static_cast<uint16_t>\(330 \+ \(step - 11\) \* 15\);/);
assert.match(speakerHeader, /constexpr uint8_t kMaxMicGainStep = 14;/);
assert.match(speakerHeader, /constexpr uint8_t kDefaultMicGainStep = 10;/);
const speakerCpp = read('src/audio/speaker.cpp');
assert.match(speakerCpp, /es7210::init\(&g_mic, assist::kSampleRate, g_mic_gain\.load\(\)\)/);
assert.match(speakerCpp, /prefs\.putUChar\(kPrefsMicGainKey, g_mic_gain\.load\(\)\);/);
console.log('Settings Audio popup checks passed.');
