import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('../../..', import.meta.url)));
const read = path => readFileSync(resolve(root, path), 'utf8');
const header = read('src/core/i18n/i18n.h');
const strings = read('src/core/i18n/i18n.cpp');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');

// The label is the last Strings field, so each locale table ends with it.
const struct = header.slice(header.indexOf('struct Strings {'));
assert.match(struct.slice(0, struct.indexOf('};')),
  /const char\* system_ip_label;\s*\/\/[^\n]*\n\s*const char\* system_mac_label;\s*$/);
for (const [table, ip, mac] of [['kStringsDe', 'IP-Adresse', 'MAC-Adresse'],
                                ['kStringsEn', 'IP address', 'MAC address'],
                                ['kStringsFr', 'Adresse IP', 'Adresse MAC'],
                                ['kStringsPl', 'Adres IP', 'Adres MAC']]) {
  const body = strings.slice(strings.indexOf(`static const Strings ${table} = {`));
  const end = body.indexOf('};');
  assert.match(body.slice(0, end + 2), new RegExp(`"${ip}",\\s*"${mac}",?\\s*\\};$`),
    `${table} ends with the IP and MAC labels`);
}

// The row sits under the Web Admin password row and follows the connection.
assert.match(settings,
  /create_security_row\(system_info_rows, tr\(\)\.system_ip_label, &system_ip_value, nullptr\);/);
assert.match(settings, /static void system_refresh_rows\(\) \{\s*system_refresh_ip_row\(\);/);
assert.match(settings, /ip \? networkTransport\.localIP\(\)\.toString\(\)\.c_str\(\) : "-"/);
assert.match(settings, /system_ip_value = nullptr;\s*system_ip_shown = 0xFFFFFFFF;\s*system_mac_value = nullptr;/,
  'teardown forgets the rows');
assert.match(settings,
  /create_security_row\(system_info_rows, tr\(\)\.system_mac_label, &system_mac_value, nullptr\);/);
const transport = read('src/network/transport/network_transport.cpp');
assert.match(transport, /if \(connected && wifi_mac_\.load\(\) == 0\)/, 'the radio MAC is read once');
assert.match(transport, /if \(activeEthernetState\(\) \|\| !isWifiConnected\(\) \|\| mac == 0\) return false;/);
assert.match(read('src/network/mqtt/mqtt_topics.cpp'), /STAT_MAC, TopicDomain::State, "mac"/);
console.log('System popup IP row checks passed.');
