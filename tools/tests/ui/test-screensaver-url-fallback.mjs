// A screensaver image URL that cannot be loaded shows the default background
// (user 2026-10-10: "if the http request fails, it should show the default
// background"): the first enabled microSD wallpaper or any image in /images,
// else black. The last URL image is dropped so a reopened screensaver does
// not bring it back, and the next successful download replaces the fallback.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const source = readRepoFile('src/ui/screensaver/image_screensaver.cpp').replace(/\r\n/g, '\n');
const fn = name => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

const fallback = fn('show_url_fallback');
assert.match(fallback, /if \(st->url_fallback\) \{/, 'shown once per failure streak');
assert.match(fallback, /const bool shown = apply_wallpaper\(st, first_enabled_wallpaper\(\), true, false\);\n\s*if \(!shown\) clear_live_wallpaper\(st\);/);
assert.match(fallback, /if \(g_cache_dsc && g_cache_name == kUrlCacheName\) \{\n\s*free_screensaver_dsc\(g_cache_dsc\);/);

// Every failure path: a failed request, a refused request, and opening
// without a usable image.
const service = fn('service_url_source');
assert.match(service, /if \(taken == UrlTake::kFailed\) \{[\s\S]*?show_url_fallback\(st\);\n\s*st->next_wallpaper_ms = now \+ url_retry_ms\(\);\n\s*return;/);
assert.match(service, /if \(!request_url_image\(\)\) show_url_fallback\(st\);\n\s*st->next_wallpaper_ms = now \+ url_retry_ms\(\);/,
  'the retry time is set after the fallback (clear_live_wallpaper zeroes it)');
const open = fn('show_url_source_on_open');
assert.match(open, /if \(taken != UrlTake::kFailed && url_cache_valid\(\)\) \{/);
assert.match(open, /const bool visible = show_url_fallback\(st\);[\s\S]*?st->next_wallpaper_ms = millis\(\) \+ url_retry_ms\(\);\n\s*return visible;/);

// A successful download ends the fallback.
assert.match(fn('take_url_image'), /present_wallpaper_dsc\(st, dsc, -1, kUrlCacheName, false\);\n\s*st->url_fallback = false;/);

// The Web Admin preview does the same when its image fails to load.
const editor = readRepoFile('src/web/admin/screensaver/editor.js').replace(/\r\n/g, '\n');
assert.match(editor, /image\.addEventListener\('error', \(\) => \{\n\s*if \(!ssImageUrlPreview\.src \|\| image\.dataset\.src !== ssImageUrlPreview\.src\) return;\n\s*ssImageUrlPreview\.failed = true;\n\s*renderScreensaverEditor\(\);/);
assert.match(editor, /if \(imageUrlMode && !\(ssImageUrlPreviewSrc\(d\) && ssImageUrlPreview\.failed\)\) \{/);
console.log('Screensaver image URL failures show the default background on panel and preview');
