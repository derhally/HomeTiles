// 2026-10-10: a new OTA image crashed on every boot of the Waveshare 7B and
// could only be recovered over USB, because setup() confirmed the image
// (esp_ota_mark_app_valid_cancel_rollback) before anything could fail. The
// image is now confirmed from loop() after a minute of running, so an early
// crash leaves it pending-verify and the bootloader rolls back.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const ino = readRepoFile('HomeTiles.ino').replace(/\r\n/g, '\n');
const body = (name) => {
  const start = ino.indexOf(name);
  assert.ok(start >= 0, name);
  let depth = 0;
  for (let i = ino.indexOf('{', start); i < ino.length; ++i) {
    if (ino[i] === '{') depth++;
    else if (ino[i] === '}' && --depth === 0) return ino.slice(start, i + 1);
  }
  throw new Error(name);
};

assert.match(ino, /static constexpr uint32_t kOtaConfirmAfterMs = 60000;/);
assert.doesNotMatch(body('void setup() {'), /confirm_running_ota_if_needed\(\);/,
  'setup() must not confirm the image');
assert.match(body('void loop() {'),
  /static bool ota_confirm_done = false;\n\s*if \(!ota_confirm_done && now >= kOtaConfirmAfterMs\) \{\n\s*ota_confirm_done = true;\n\s*confirm_running_ota_if_needed\(\);/);
assert.match(body('static void confirm_running_ota_if_needed() {'),
  /if \(ota_state != ESP_OTA_IMG_PENDING_VERIFY\) \{\n\s*return;\n\s*\}\n\n\s*const esp_err_t mark_err = esp_ota_mark_app_valid_cancel_rollback\(\);/);
console.log('OTA images are confirmed only after a minute of running');
