// Settings > WiFi shows the station MAC under the connected network once WiFi
// reported it (as the router lists it); the panel publishes it over MQTT for
// the Bridge's MAC sensor. The row's label is translated in every language.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const header = readRepoFile('src/core/i18n/i18n.h');
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const screen = readRepoFile('src/ui/tabs/settings/settings_screen.cpp');
const model = readRepoFile('src/ui/tabs/settings/tab_settings.cpp');
const transport = readRepoFile('src/network/transport/network_transport.h');
const preview = readRepoFile('tools/settings-preview.mjs');

const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const fields = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(match => match[1]);
const index = fields.indexOf('settings_mac_address');
assert.ok(index >= 0, 'settings_mac_address is in the Strings struct');
const expected = {kStringsDe: 'MAC-Adresse', kStringsEn: 'MAC address', kStringsFr: 'Adresse MAC', kStringsPl: 'Adres MAC'};
for (const [table, value] of Object.entries(expected)) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)].map(m => m[1]);
  assert.equal(values.length, fields.length, `${table} has one value per field`);
  assert.equal(values[index], value, `${table} MAC label`);
}

assert.match(transport, /bool activeMac\(char\* out, size_t len\) const;/);
assert.match(model, /bool wifi_mac\(char\* buf, size_t len\) \{ return networkTransport\.activeMac\(buf, len\); \}/);
const page = screen.slice(screen.indexOf('void build_wifi_page(lv_obj_t* page) {'), screen.indexOf('// ---------- WiFi entry ----------'));
// Only under a connected WiFi network, and only once the MAC is known.
assert.match(page, /if \(!v\.access_point && v\.connected\) \{[\s\S]*?wifi_button\(status\.row, s\.wifi_disconnect_btn, WifiAction::Disconnect\);\s*\/\/[^\n]*\n\s*char mac\[24\];\s*if \(settings_model::wifi_mac\(mac, sizeof\(mac\)\)\) \{\s*settings_parts::Row address = settings_parts::row\(group, "identifier", s\.settings_mac_address\);\s*settings_parts::trailing_text\(address\.row, mac, g_page_width \/ 2\);/);
assert.match(preview, /bool wifi_mac\(char\* buf, size_t len\)/);

console.log('WiFi MAC row: translated label, shown once known, previewed');
