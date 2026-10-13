import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('../../..', import.meta.url)));
const read = path => readFileSync(resolve(root, path), 'utf8');
const header = read('src/core/i18n/i18n.h');
const strings = read('src/core/i18n/i18n.cpp');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');
const transport = read('src/network/transport/network_transport.cpp');

// The label is the last Strings field, translated in every locale.
const struct = header.slice(header.indexOf('struct Strings {'));
assert.match(struct.slice(0, struct.indexOf('};')), /const char\* wifi_mac_label;\s*$/);
for (const [table, label] of [['kStringsDe', 'MAC-Adresse'], ['kStringsEn', 'MAC address'],
                              ['kStringsFr', 'Adresse MAC'], ['kStringsPl', 'Adres MAC']]) {
  const body = strings.slice(strings.indexOf(`static const Strings ${table} = {`));
  assert.match(body.slice(0, body.indexOf('};') + 2), new RegExp(`"${label}",?\\s*\\};$`), table);
}

// The System popup keeps its three rows and an empty status area; the Wi-Fi
// card shows the MAC under the connection status (which already names the IP).
assert.doesNotMatch(settings, /system_ip_label|system_mac_label|system_refresh_network_status/);
assert.match(settings, /if \(networkTransport\.activeMac\(mac, sizeof\(mac\)\)\) \{\s*snprintf\(buf, sizeof\(buf\), "%s: %s", tr\(\)\.wifi_mac_label, mac\);/);
assert.match(settings, /if \(wifi_mac_label\) lv_obj_add_flag\(wifi_mac_label, LV_OBJ_FLAG_HIDDEN\);\s*lv_label_set_text\(wifi_conn_status_label, tr\(\)\.wifi_ap_active\);/,
  'access point mode hides the station MAC');
assert.match(settings, /wifi_mac_label = nullptr;/, 'teardown forgets the label');
// The radio MAC is read once, and only Wi-Fi reports one.
assert.match(transport, /if \(connected && wifi_mac_\.load\(\) == 0\)/);
assert.match(transport, /if \(activeEthernetState\(\) \|\| !isWifiConnected\(\) \|\| mac == 0\) return false;/);
assert.match(read('src/network/mqtt/mqtt_topics.cpp'), /STAT_MAC, TopicDomain::State, "mac"/);
console.log('Wi-Fi MAC line checks passed.');
