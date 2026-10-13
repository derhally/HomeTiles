import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('../../..', import.meta.url)));
const read = path => readFileSync(resolve(root, path), 'utf8');
const battery = read('src/core/power/battery_state.cpp');
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
const topics = read('src/network/mqtt/mqtt_topics.cpp');
const bridge = read('src/network/bridge/ha_bridge_config.cpp');

// The 7B reads its cell on GPIO20 through the 200k/100k divider; every other
// profile keeps the stub.
const sevenB = battery.slice(battery.indexOf('#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)'),
                             battery.indexOf('#else'));
assert.match(sevenB, /kAdcChannel = static_cast<adc_channel_t>\(ADC1_GPIO20_CHANNEL\);/);
assert.match(sevenB, /kAdcUnit = ADC_UNIT_1;/);
assert.match(sevenB, /kDividerNumerator = 3;/);
assert.match(sevenB, /batteryStateSupportsMeasurement\(\) \{ return true; \}/);
assert.match(sevenB, /batteryStateIsOnMains\(\) \{ return true; \}/,
  'Mains presence is not measurable on the 7B; keep the mains sleep profile');
const stub = battery.slice(battery.indexOf('#else'));
assert.match(stub, /batteryStateSupportsMeasurement\(\) \{ return false; \}/);

// Mirror the voltage-to-level curve and check a few points.
const curve = [...sevenB.matchAll(/\{(\d{4}), (\d{1,3})\}/g)].map(m => [+m[1], +m[2]]);
const pct = mv => {
  if (mv <= curve[0][0]) return 0;
  if (mv >= curve.at(-1)[0]) return 100;
  for (let i = 1; i < curve.length; i++) {
    if (mv <= curve[i][0]) {
      const [m0, p0] = curve[i - 1];
      const [m1, p1] = curve[i];
      return p0 + Math.trunc(((mv - m0) * (p1 - p0) + Math.trunc((m1 - m0) / 2)) / (m1 - m0));
    }
  }
  return 100;
};
assert.equal(curve.length, 12);
for (let i = 1; i < curve.length; i++) {
  assert.ok(curve[i][0] > curve[i - 1][0] && curve[i][1] > curve[i - 1][1], 'curve must rise');
}
assert.equal(pct(3200), 0);
assert.equal(pct(3820), 50);
assert.equal(pct(4050), 85);
assert.equal(pct(4300), 100);

// Topics and publishing.
assert.match(topics, /SENSOR_BATTERY_V, TopicDomain::Sensor, "battery_voltage"/);
assert.match(topics, /SENSOR_WIFI_RSSI, TopicDomain::Sensor, "wifi_rssi"/);
assert.match(topics, /STAT_RESET_REASON, TopicDomain::State, "reset_reason"/);
assert.match(topics, /SENSOR_FREE_INTERNAL, TopicDomain::Sensor, "free_internal_kib"/);
assert.match(topics, /SENSOR_FREE_PSRAM, TopicDomain::Sensor, "free_psram_kib"/);
assert.match(topics, /SENSOR_CHIP_TEMP, TopicDomain::Sensor, "chip_temperature"/);
assert.match(topics, /STAT_FIRMWARE, TopicDomain::State, "firmware_version"/);
const diag = mqtt.slice(mqtt.indexOf('static void publish_runtime_diagnostics('),
                        mqtt.indexOf('void mqttPublishHomeSnapshot()'));
assert.match(diag, /kIntervalMs = 60000/);
assert.match(diag, /if \(force\) \{\s*networkManager\.mqttEnqueuePublish\(mqttTopics\.topic\(TopicKey::STAT_RESET_REASON\)/);
assert.match(diag, /TopicKey::STAT_FIRMWARE\), FW_VERSION, true/);
assert.match(diag, /chip_temperature::read\(&chip_c\) && std::isfinite\(chip_c\)/);
assert.match(diag, /"unavailable"/, 'Missing values must be explicit, not zero');
const service = mqtt.slice(mqtt.indexOf('void mqttServiceLocalSensors()'));
assert.match(service, /publish_runtime_diagnostics\(false\)/);
assert.match(mqtt, /if \(!force && soc == g_last_synced_soc\) return;/,
  'The 500 ms service must not rebuild sensor maps for an unchanged level');

// Bridge capabilities.
assert.match(bridge, /\\"battery_voltage\\":/);
assert.match(bridge, /\\"runtime_diagnostics\\":true/);
console.log('Battery and runtime diagnostics checks passed.');
