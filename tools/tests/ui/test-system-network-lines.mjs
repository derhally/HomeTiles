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

// Five rows overflowed the 600 px layout, so the idle status area above the
// buttons shows the IP and MAC address instead of extra rows.
assert.doesNotMatch(settings, /create_security_row\(system_info_rows, tr\(\)\.system_(ip|mac)_label/);
assert.equal((settings.match(/create_security_row\(system_info_rows,/g) || []).length, 3,
  'the System popup keeps exactly three rows');
assert.match(settings, /static void system_refresh_rows\(\) \{\s*system_refresh_network_status\(\);/);
assert.match(settings, /if \(!system_status_label \|\| system_status_message\) return;/,
  'an update or error message keeps priority over the network lines');
assert.match(settings, /"%s: %s\\n%s: %s", tr\(\)\.system_ip_label, ip,\s*tr\(\)\.system_mac_label, mac\);/);
assert.match(settings, /system_status_message = false;\s*system_network_shown\[0\] = '\\0';\s*system_status_text\(""[^)]*\);\s*system_refresh_network_status\(\);/);
const transport = read('src/network/transport/network_transport.cpp');
assert.match(transport, /if \(connected && wifi_mac_\.load\(\) == 0\)/, 'the radio MAC is read once');
assert.match(transport, /if \(activeEthernetState\(\) \|\| !isWifiConnected\(\) \|\| mac == 0\) return false;/);
assert.match(read('src/network/mqtt/mqtt_topics.cpp'), /STAT_MAC, TopicDomain::State, "mac"/);
console.log('System popup network lines checks passed.');
