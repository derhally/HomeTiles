// Keys, nonces, challenges, session ids and pairing codes need true random
// bytes. The ESP32-P4 has no radio, so Espressif documents its RNG as
// pseudo-random unless the SAR ADC entropy source runs; secure_random::fill
// switches that source on around each read. The source must not be shared
// with an ADC user, and the security code must not bypass the helper.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import {readRepoFile, repoRoot} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const helper = maskCpp(readRepoFile('src/core/security/secure_random.cpp'));
const radioless = helper.slice(helper.indexOf('#if !SOC_WIFI_SUPPORTED && !SOC_BT_SUPPORTED\n  '),
  helper.indexOf('#else'));
const steps = ['sar_adc::Lock adc_lock;', 'bootloader_random_enable();',
  'esp_fill_random(out, length);', 'bootloader_random_disable();'];
let previous = -1;
for (const step of steps) {
  const index = radioless.indexOf(step);
  assert.ok(index > previous, `radio-less chips run "${step}" in order inside the shared ADC lock`);
  previous = index;
}
assert.match(helper, /#else\s*esp_fill_random\(out, length\);\s*#endif/,
  'chips with a radio read the RNG directly');

// The security code takes every random byte from the helper.
const securitySources = [
  'src/network/secure/command_channel.cpp',
  'src/network/secure/command_channel_core.h',
  'src/web/server/auth/web_admin_auth.cpp',
  'src/web/server/auth/web_admin_auth_core.h',
  'src/core/security/x25519.cpp',
];
for (const file of securitySources) {
  const source = maskCpp(readRepoFile(file));
  assert.doesNotMatch(source, /\besp_fill_random\s*\(|\besp_random\s*\(/,
    `${file} uses secure_random::fill instead of the raw RNG`);
}
const channel = maskCpp(readRepoFile('src/network/secure/command_channel.cpp'));
for (const buffer of ['nonce', 'challenge', 'id', 'g_attempt->secret', 'g_attempt->n_p']) {
  const name = buffer.replace('->', '\\->');
  assert.match(channel, new RegExp(`secure_random::fill\\(${name}, sizeof\\(${name}\\)\\);`),
    `the command channel ${buffer} comes from secure_random::fill`);
}
assert.match(maskCpp(readRepoFile('src/core/security/x25519.cpp')),
  /int randomBytes\(void\*, unsigned char\* out, size_t length\) \{\s*secure_random::fill\(out, length\);/,
  'the mbedTLS blinding random comes from secure_random::fill');
const auth = maskCpp(readRepoFile('src/web/server/auth/web_admin_auth.cpp'));
assert.match(auth, /void fillRandom\(uint8_t\* out, size_t length\) \{\s*secure_random::fill\(out, length\);\s*\}/,
  'Web Admin nonces, sessions and CSRF tokens come from secure_random::fill');

// bootloader_random_enable() resets and reprograms the SAR ADC. The only ADC
// users are the measurements below; each opens and closes its driver inside
// the shared sar_adc::Lock so it never reads a peripheral reset by the
// entropy source.
function sourceFiles(directory) {
  return fs.readdirSync(path.join(repoRoot, directory), {withFileTypes: true}).flatMap(entry => {
    const relative = path.join(directory, entry.name);
    if (entry.isDirectory()) return relative.endsWith(path.join('web', 'generated')) ? [] : sourceFiles(relative);
    return /\.(c|cpp|h|ino)$/.test(entry.name) ? [relative] : [];
  });
}
const adcUse = /\banalogRead(?:Milli[Vv]olts)?\s*\(|\badc_oneshot_|\badc_continuous_|\badc_cali_|\btemperature_sensor_|\btemperatureRead\s*\(/;
const lockedAdcUsers = {
  [path.join('src', 'core', 'power', 'battery_state.cpp')]: ['adc_oneshot_new_unit(', 'adc_oneshot_del_unit(unit);'],
  [path.join('src', 'core', 'hardware', 'chip_temperature.cpp')]: ['temperature_sensor_install(', 'temperature_sensor_uninstall(sensor);'],
};
const firmwareSources = [...sourceFiles('src'), 'HomeTiles.ino'];
for (const file of firmwareSources) {
  const source = maskCpp(readRepoFile(file));
  const steps = lockedAdcUsers[path.normalize(file)];
  if (!steps) {
    assert.doesNotMatch(source, adcUse,
      `${file} uses the ADC, which conflicts with the P4 entropy source in secure_random.cpp`);
    continue;
  }
  assert.doesNotMatch(source, /\banalogRead(?:Milli[Vv]olts)?\s*\(|\btemperatureRead\s*\(|\badc_continuous_/,
    `${file} must not use Arduino's persistent ADC handles or continuous mode`);
  let previous = source.indexOf('sar_adc::Lock adc_lock;');
  assert.ok(previous >= 0, `${file} takes the shared ADC lock`);
  for (const step of steps) {
    const index = source.indexOf(step, previous);
    assert.ok(index > previous, `${file} runs "${step}" inside the shared ADC lock`);
    previous = index;
  }
}
console.log(`Secure random: P4 entropy source paired, security code uses it, ADC users share its lock in ${firmwareSources.length} files`);
