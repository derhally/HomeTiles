// The screensaver can load every slide from an image URL template instead of
// the microSD card. The panel fills in ${width}, ${height} and ${timestamp};
// the Web Admin preview, the stored configuration and the firmware must agree
// on the field name and on which templates are valid.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const template = 'https://images.example.net/${width}/${height}?random=${timestamp}&resizemode=fit';
const validity = [
  ['', true],
  [template, true],
  ['http://192.168.1.5:8080/img.jpg', true],
  ['ftp://example.net/a.jpg', false],
  ['https://', false],
  ['https:///path', false],
  ['https://example.net/a b.jpg', false],
  ['example.net/a.jpg', false],
  ['https://example.net/' + 'a'.repeat(512), false],
];

// Firmware: the pure helpers the config store and the screensaver use.
const harness = `
#include <iostream>
#include <string>
#include "src/ui/screensaver/screensaver_image_url.h"
int main() {
  std::string line;
  std::getline(std::cin, line);
  std::cout << screensaver_image_url::expand(line, 1024, 600, 1791638329ULL) << "\\n";
  std::cout << screensaver_image_url::expand("https://x.net/\${width}\${width}/\${other}/$", 7, 8, 9) << "\\n";
  while (std::getline(std::cin, line)) {
    std::cout << (screensaver_image_url::valid(line) ? 1 : 0) << "\\n";
  }
  return 0;
}
`;
const input = [template, ...validity.map(([url]) => url)].join('\n') + '\n';
const output = compileAndRun({
  label: 'Screensaver image URL helpers',
  harness,
  sources: ['src/ui/screensaver/screensaver_image_url.cpp'],
  input,
});
if (output !== null) {
  const lines = output.trimEnd().split('\n');
  assert.equal(lines[0],
    'https://images.example.net/1024/600?random=1791638329&resizemode=fit');
  assert.equal(lines[1], 'https://x.net/77/${other}/$');
  assert.deepEqual(lines.slice(2).map(Number),
    validity.map(([, ok]) => (ok ? 1 : 0)));
}

// Browser: the same validity rules and the preview's substitution.
const admin = readRepoFile('src/web/admin/screensaver/editor.js');
const extract = name => {
  const start = admin.indexOf(`function ${name}(`);
  assert.ok(start >= 0, `${name} missing from admin.js`);
  let depth = 0;
  for (let i = admin.indexOf('{', start); i < admin.length; i++) {
    if (admin[i] === '{') depth++;
    if (admin[i] === '}' && --depth === 0) return admin.slice(start, i + 1);
  }
  throw new Error(`${name} is not closed`);
};
const browser = new Function(`
  let ssImageUrlPreview = { template: '', src: '' };
  ${extract('ssImageUrlValid')}
  ${extract('ssImageUrlPreviewSrc')}
  return { ssImageUrlValid, ssImageUrlPreviewSrc };
`)();
for (const [url, ok] of validity) {
  assert.equal(browser.ssImageUrlValid(url), ok, `browser validity of ${url}`);
}
const previewSrc = browser.ssImageUrlPreviewSrc(
  {image_url: template, screen_width: 1024, screen_height: 600});
assert.match(previewSrc,
  /^https:\/\/images\.example\.net\/1024\/600\?random=\d{10}&resizemode=fit$/);
// One image per template: re-rendering never re-requests with a new timestamp.
assert.equal(browser.ssImageUrlPreviewSrc(
  {image_url: template, screen_width: 1024, screen_height: 600}), previewSrc);

// The editor loads, binds, renders and posts image_url.
assert.match(admin, /data\.image_url = typeof data\.image_url === 'string'/);
assert.match(admin, /image_url: String\(d\.image_url \|\| ''\)\.trim\(\)/);
assert.match(admin, /getElementById\('screensaverImageUrl'\)/);
assert.match(admin, /screensaverDraft\.image_url = url;\s*renderScreensaverEditor\(\);\s*scheduleScreensaverSave\(\);/);

// Web Admin HTML: a localized label and hint beside the field.
const html = readRepoFile('src/web/server/render/web_admin_html.cpp');
assert.match(html, /tr\.screensaver_image_url;[\s\S]*id="screensaverImageUrl"[\s\S]*tr\.screensaver_image_url_hint;/);
assert.match(html, /id="screensaverWallpaperFraming"/);

// Every language carries both strings, placed right after the storage hint.
const i18nHeader = readRepoFile('src/core/i18n/i18n.h');
assert.match(i18nHeader,
  /const char\* screensaver_storage_hint;[\s\S]*?const char\* screensaver_image_url;\s*const char\* screensaver_image_url_hint;/);
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
for (const label of ['"Bild-URL"', '"Image URL"', '"URL de l\'image"', '"Adres URL obrazu"']) {
  const at = i18n.indexOf(label);
  assert.ok(at > 0, `${label} missing`);
  const before = i18n.slice(i18n.lastIndexOf('\n', i18n.lastIndexOf('\n', at) - 1), at);
  assert.match(before, /microSD/, `${label} must follow the storage hint`);
  assert.match(i18n.slice(at, i18n.indexOf('\n', i18n.indexOf('\n', at) + 1)),
    /\$\{width\}, \$\{height\}/, `${label} hint must name the variables`);
}

// Stored configuration: loaded, normalized, saved and rejected when invalid.
const config = readRepoFile('src/ui/screensaver/screensaver_config.cpp');
assert.match(config, /loaded\.image_url = String\(doc\["image_url"\] \| ""\);/);
assert.match(config, /data_\.image_url = safe_image_url\(data_\.image_url\);/);
assert.match(config, /doc\["image_url"\] = data_\.image_url;/);
assert.match(config, /error = "Invalid image URL";/);

// Runtime: the download runs on its own worker, never in the LVGL timer.
const runtime = readRepoFile('src/ui/screensaver/image_screensaver.cpp');
assert.match(runtime, /if \(url_source_active\(\)\) \{\s*service_url_source\(st, now\);/);
assert.match(runtime, /url_source_active\(\) \? show_url_source_on_open\(st\)/);
assert.doesNotMatch(runtime, /HTTPClient/);
const fetcher = readRepoFile('src/ui/screensaver/screensaver_url_fetch.cpp');
assert.match(fetcher, /xTaskCreateWithCaps\(worker_task/);
assert.doesNotMatch(fetcher, /lv_/);

console.log('Screensaver image URL contract passed.');
