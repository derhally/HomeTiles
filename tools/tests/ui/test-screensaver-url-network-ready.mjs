// Boot loop on the Waveshare 7B (2026-10-10): with an image URL saved, the
// screensaver preload asked for the first image about 3 s after boot, before
// Wi-Fi was up. The HTTPS DNS lookup reached lwIP without its tcpip thread
// and asserted (tcpip_send_msg_wait_sem "Invalid mbox") on every start.
// The URL worker now refuses, and the download itself skips, until the
// network transport is connected with an address.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const source = readRepoFile('src/ui/screensaver/screensaver_url_fetch.cpp').replace(/\r\n/g, '\n');
const fn = name => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name || f.name.endsWith('::' + name));
  assert.ok(found, name);
  return found.source;
};

assert.match(source, /#include "src\/network\/transport\/network_transport\.h"/);
assert.match(fn('network_ready'),
  /return networkTransport\.isConnected\(\) &&\n\s*static_cast<uint32_t>\(networkTransport\.localIP\(\)\) != 0;/);

// The guard comes before any HTTP object is built or any socket is opened.
const download = fn('download');
const guard = download.indexOf('if (!network_ready())');
assert.ok(guard >= 0, 'download checks the network');
assert.ok(guard < download.indexOf('HTTPClient http;'), 'before HTTPClient');
assert.ok(guard < download.indexOf('http.GET()'), 'before the request');

// No request is queued (and no worker started) while the network is down.
const request = fn('request');
const requestGuard = request.indexOf('if (!network_ready())');
assert.ok(requestGuard >= 0, 'request checks the network');
assert.ok(requestGuard < request.indexOf('ensure_worker()'), 'before the worker starts');

// The skip log is rate-limited: the slideshow retries on every interval.
assert.match(fn('log_network_not_ready'), /if \(last_log_ms != 0 && now - last_log_ms < 30000\) return;/);
console.log('Screensaver URL fetch waits for a connected network before DNS and sockets');
