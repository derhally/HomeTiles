// Every history popup put 24H (Today) left of 7D. The longer range reaches
// further into the past, so 7D now sits left (numeric Sensor graphs continue
// with 12H, 1H and 30m): Sensor (also Binary Sensor,
// Number, Select and Date/Time history and Activity), Weather and Energy.
// Energy's day button names today instead of 24H, since it counts from
// midnight.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const sensor = readRepoFile('src/ui/popups/sensor/sensor_popup.cpp');
assert.match(sensor, /ctx->range_week_btn = make_range_button\(range_labels\[4\]\);\s*ctx->range_day_btn = make_range_button\(range_labels\[3\]\);\s*ctx->range_12h_btn = make_range_button\(range_labels\[2\]\);\s*ctx->range_1h_btn = make_range_button\(range_labels\[1\]\);\s*ctx->range_30m_btn = make_range_button\(range_labels\[0\]\);/,
  'Sensor history: 7D, 24H, 12H, 1H, 30m from left to right, longest first');

const energy = readRepoFile('src/ui/popups/energy/energy_popup.cpp');
assert.match(energy, /ctx->week_btn = make_button_label\(period_row, "7D", &ctx->week_label\);\s*ctx->day_btn = make_button_label\(period_row, today_button_label\(\), &ctx->day_label\);/,
  'Energy: 7D left of Today');
assert.doesNotMatch(energy, /"24H"/, 'Energy shows no 24H label');
assert.match(energy, /void update_period_buttons\([\s\S]*?fit_today_button\(ctx\);\s*\}/,
  'Each opening sets Today in the current display language');

const weather = readRepoFile('src/ui/popups/weather/weather_popup.cpp');
assert.match(weather, /header_week_btn = make_header_action_button\("7D", kFooterOuterActionX, FONT_UNIT\);/,
  'Weather: 7D takes the outer slot');
assert.match(weather, /make_header_action_button\(weather_today_button_text\(\), kFooterInnerActionX, FONT_UNIT\);/,
  'Weather: Today takes the inner slot next to the day navigation');
assert.match(weather, /const int week_x = today_day >= 0 \? kFooterOuterActionX - today_extra : kFooterInnerActionX;/,
  'Weather: without Today, 7D closes the gap to the day navigation');

console.log('History popups: 7D left of 24H/Today; Energy says Today');
