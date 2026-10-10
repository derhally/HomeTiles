// Tile color None (user 2026-10-10): every tile can be transparent, as a
// fourth choice beside Global, Custom and From icon. It replaces the
// screensaver's "Fill background" checkbox. None is a spare bit of the stored
// tile color (PackedTileV7 unchanged), so older files never turn transparent,
// and screensaver tiles saved without fill become None.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = p => readRepoFile(p).replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

// Storage: a bit beside TILE_BG_COLOR_EXPLICIT; the color under it still
// follows the global color or keeps its own.
const config = read('src/tiles/config/tile_config.h');
assert.match(config, /static constexpr uint32_t TILE_BG_TRANSPARENT = 0x02000000u;/);
assert.match(config, /return \(tile\.bg_color & ~TILE_BG_TRANSPARENT\) != 0;/);
assert.match(config, /stored &= ~TILE_BG_TRANSPARENT;\n\s*return stored == 0 \|\| tile_color::isDefaultGrey\(stored\);/);
assert.match(config, /if \(tileBgTransparent\(tile\)\) return false;/, 'no outline without background');
assert.doesNotMatch(config, /ss_fill/, 'the screensaver fill flag is gone');

// Panel: normal grids in render_tile, the screensaver after its own opacity.
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
const transparent = fn(renderer, 'apply_transparent_tile_card');
assert.match(transparent, /lv_obj_set_style_bg_opa\(card, LV_OPA_TRANSP, LV_PART_MAIN \| LV_STATE_DEFAULT\);/);
assert.match(transparent, /ui_surface_style::disable_tile_border\(card\);/);
assert.match(transparent, /tile_icon_disc::refresh_fills\(card\);\n\s*tile_icon_source::refresh_controls\(card\);/);
assert.match(fn(renderer, 'render_tile'),
  /grid_type != GridType::SCREENSAVER && tile\.type != TILE_EMPTY &&\s*tileBgTransparent\(tile\)\) \{\n\s*apply_transparent_tile_card\(tile_obj\);/);
assert.match(fn(read('src/ui/screensaver/image_screensaver.cpp'), 'apply_slot_look'),
  /if \(tileBgTransparent\(tile\)\) apply_transparent_tile_card\(card\);/);

// Endpoint: None over the chosen color; older screensaver data and exports
// with "Fill background" off become None.
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /if \(server\.hasArg\("bg_color_none"\) && server\.arg\("bg_color_none"\)\.toInt\(\) != 0\) \{\n\s*tile\.bg_color \|= TILE_BG_TRANSPARENT;/);
assert.match(tiles, /if \(server\.hasArg\("ss_fill"\) && server\.arg\("ss_fill"\)\.toInt\(\) == 0\) \{\n\s*tile\.bg_color \|= TILE_BG_TRANSPARENT;/);
assert.ok(tiles.indexOf('bg_color_none') > tiles.indexOf('makeTileBgColor(static_cast<uint32_t>(server.arg("bg_color")'),
  'None is applied after the color');
const looks = read('src/ui/screensaver/screensaver_config.cpp');
assert.match(looks, /if \(!\(item\["fill"\] \| true\)\) tile\.bg_color \|= TILE_BG_TRANSPARENT;/);
assert.doesNotMatch(looks, /item\["fill"\] =/, 'the old fill flag is no longer written');

// Web Admin: a fifth tile color button, translated everywhere; no checkbox.
const html = read('src/web/server/render/web_admin_html.cpp');
assert.match(html, /\{"none", tr\.tile_color_mode_none\}/);
assert.doesNotMatch(html, /_tile_ss_fill/);
assert.match(html, /cssClass \+= " tile-bg-none";\n\s*tileStyle = "background:transparent";/);
assert.ok(read('src/core/i18n/i18n.h').includes('const char* tile_color_mode_none;'));
const i18n = read('src/core/i18n/i18n.cpp');
for (const pair of [['"Aus Cover",', '"Keine",'], ['"From cover",', '"None",'],
                    ['"De la pochette",', '"Aucune",'], ['"Z okładki",', '"Brak",']]) {
  assert.ok(i18n.includes(pair[0] + '\n    ' + pair[1]), pair[1]);
}

// Browser helpers: the bit never reads as a color.
const context = {document: {getElementById: id => dom[id] || null}};
const dom = {};
vm.createContext(context);
vm.runInContext(['rgbToHex', 'tileBgValueIsSet', 'tileBgToHex', 'isDefaultTileGrey', 'tileBgFollowsDefault',
  'tileColorMode', 'tileColorInputIsNone', 'snapshotBgColorIsNone']
  .map(extractDeliveredFunction).join('\n'), context);
const call = (name, ...args) => vm.runInContext(name, context)(...args);
assert.equal(call('tileBgFollowsDefault', 0x02000000), true, 'None over the global color');
assert.equal(call('tileBgValueIsSet', 0x02000000), false);
assert.equal(call('tileBgToHex', 0x02000000 | 0x01000000 | 0x336699, '#111111'), '#336699');
assert.equal(call('tileBgFollowsDefault', 0x01000000 | 0x336699), false);
dom.t_tile_color = {dataset: {bgColorNone: '1', bgColorDefault: '1'}};
dom.t_tile_icon_fill = {checked: true};
assert.equal(call('tileColorMode', 't'), 'none', 'None wins over a stale icon fill');
assert.equal(call('tileColorInputIsNone', 't'), true);
dom.t_tile_color.dataset.bgColorNone = '0';
assert.equal(call('tileColorMode', 't'), 'icon');
assert.equal(call('snapshotBgColorIsNone', {bg_color_none: '1'}), true);

// Every path that carries a tile color carries None.
for (const file of ['snapshots', 'clipboard', 'drafts']) {
  assert.match(read(`src/web/admin/tiles/${file}.js`), /bg_color_none: tileColorInputIsNone\(tab\) \? '1' : '0',/, file);
}
const grid = read('src/web/admin/tiles/grid-preview.js');
assert.match(grid, /input\.dataset\.bgColorNone = mode === 'none' \? '1' : '0';/);
assert.match(grid, /const bgNone = typeValue !== '0' && \(Number\(tile\.bg_color\) & 0x02000000\) !== 0;/);
assert.match(read('src/web/admin/tiles/live-preview.js'), /const bgNone = tileColorInputIsNone\(tab\);/);
assert.match(read('src/web/admin/tiles/snapshots.js'), /\(String\(snapshot\?\.bg_color_none \|\| '0'\) === '1' \? 0x02000000 : 0\);/);
assert.match(read('src/web/admin/tiles/import-export.js'), /fd\.append\('bg_color_none', bgNone \? '1' : '0'\);/);
console.log('Tile color None: stored, served, drawn and previewed on every grid');
