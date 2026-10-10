// Per-tile screensaver look (user 2026-10-10): every screensaver tile sets
// its whole-card opacity, whether its background is filled, whether its icon
// shows and an own text color. The look lives in the Tile, so it moves with
// the tile; it is stored beside the screensaver grid, read and written by the
// tile endpoint, drawn by the panel and by both Web Admin previews.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = p => readRepoFile(p).replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

// Tile fields with neutral defaults, part of the content equality so a look
// change rebuilds the card.
const tileHeader = read('src/tiles/config/tile_config.h');
for (const field of [/uint8_t ss_opacity = 255;/, /bool ss_show_icon = true;/,
                     /uint32_t ss_text_color = 0;/, /uint8_t ss_text_align = 0;/]) {
  assert.match(tileHeader, field);
}
assert.match(tileHeader, /a\.ss_opacity == b\.ss_opacity &&\s*a\.ss_show_icon == b\.ss_show_icon &&\s*a\.ss_text_color == b\.ss_text_color && a\.ss_text_align == b\.ss_text_align;/);

// Persistence: written with the grid, read back on load and when a failed
// save restores the stored grid; empty tiles drop their look.
const config = read('src/ui/screensaver/screensaver_config.cpp');
assert.match(config, /constexpr char kTileLooksPath\[\] = "\/_screensaver\/tile_looks\.json";/);
const replace = fn(config, 'ScreensaverConfigStore::replaceTileGrid');
assert.ok(replace.indexOf('save_tile_looks(*candidate)') > replace.indexOf('tileConfig.saveScreensaverGrid(*candidate)'));
assert.ok(replace.indexOf('if (saved) gridStorage() = *candidate;') > replace.indexOf('save_tile_looks(*candidate)'),
  'the RAM grid changes only after both files are written');
const load = fn(config, 'ScreensaverConfigStore::load');
assert.ok(load.indexOf('applyStoredTileLooks(gridStorage());') > load.indexOf('tileConfig.loadScreensaverGrid(gridStorage())'));
assert.match(fn(config, 'ScreensaverConfigStore::normalizeTileGrid'),
  /if \(tile\.type == TILE_EMPTY\) \{\n\s*tile\.background_opacity = 0;\n\s*screensaver_tile_look::reset\(tile\);/);
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /tileConfig\.loadScreensaverGrid\(\*stored\)\) \{\n\s*screensaverConfig\.applyStoredTileLooks\(\*stored\);/);

// Endpoint: GET and POST use the same names, on the screensaver grid only;
// partial requests keep the stored look.
for (const key of ['ss_opacity', 'ss_icon', 'ss_text_color', 'ss_text_align']) {
  assert.ok(tiles.includes(`\\"${key}\\":`), 'GET ' + key);
  assert.ok(tiles.includes(`server.hasArg("${key}")`), 'POST ' + key);
}
assert.match(tiles, /if \(screensaver_grid\) \{\n\s*\/\/ Screensaver look: partial requests keep the stored values\./);

// Text color helpers: black is a valid own color, junk is none.
const look = read('src/ui/screensaver/screensaver_tile_look.h');
assert.match(look, /inline constexpr uint32_t kTextColorSet = 0x01000000u;/);
assert.match(look, /if \(text\.length\(\) != 7 \|\| text\[0\] != '#'\) return 0;/);

// Panel: whole-card opacity in DEFAULT and PRESSED, no fill, no outline
// without fill, hidden icons and own text color through an unused state.
const panel = read('src/ui/screensaver/image_screensaver.cpp');
const apply = fn(panel, 'apply_slot_look');
assert.match(apply, /set_opa_if_changed\(card, LV_STYLE_OPA, tile\.ss_opacity, LV_PART_MAIN \| LV_STATE_DEFAULT\);/);
assert.match(apply, /set_opa_if_changed\(card, LV_STYLE_OPA, tile\.ss_opacity, LV_PART_MAIN \| LV_STATE_PRESSED\);/);
assert.match(apply, /if \(tileBgTransparent\(tile\)\) apply_transparent_tile_card\(card\);/);
const children = fn(panel, 'apply_look_to_children');
assert.match(children, /if \(!tile\.ss_show_icon\) set_opa_if_changed\(child, LV_STYLE_OPA, LV_OPA_TRANSP, 0\);/);
assert.match(children, /if \(!tile\.ss_show_icon\) set_opa_if_changed\(child, LV_STYLE_TEXT_OPA, LV_OPA_TRANSP, 0\);/);
assert.match(children, /lv_obj_set_style_text_color\(child, color, kLookTextColor\);/);
assert.match(panel, /constexpr lv_style_selector_t kLookTextColor = LV_PART_MAIN \| LV_STATE_USER_4;/);
assert.match(fn(panel, 'build_slot_tile'), /apply_slot_look\(tile_obj, tile\);\n[\s\S]*?if \(opacity < LV_OPA_COVER \|\| tileBgTransparent\(tile\)\) \{/);
assert.match(fn(panel, 'apply_slot_tile_borders'), /apply_tile_border\(card, enabled && !tileBgTransparent\(grid\.tiles\[i\]\)\);/);
assert.match(fn(panel, 'refresh_slot_values'), /process_media_update_queue\(\);\n\s*reapply_slot_looks\(st\);/);
// The text-color state must stay free of the stash styles on labels.
for (const file of ['src/tiles/runtime/tile_icon_disc.h', 'src/ui/shared/ui_surface_style.cpp']) {
  for (const line of read(file).split('\n').filter(l => l.includes('LV_STATE_USER_4'))) {
    assert.match(line, /kCardTintStore|kBorderTintStore/, file + ': ' + line);
  }
}

// Web Admin controls, central translations in every language.
const html = read('src/web/server/render/web_admin_html.cpp');
for (const id of ['_tile_ss_icon', '_tile_ss_opacity', '_tile_ss_text_color_on', '_tile_ss_text_color']) {
  assert.ok(html.includes(id), id);
}
const i18nHeader = read('src/core/i18n/i18n.h');
for (const key of ['screensaver_tile_show_icon', 'screensaver_tile_look_opacity', 'screensaver_tile_text_color']) {
  assert.ok(i18nHeader.includes(`const char* ${key};`), key);
}
const i18n = read('src/core/i18n/i18n.cpp');
for (const label of ['"Show icon",', '"Own text color",',
                     '"Icon anzeigen",', '"Eigene Textfarbe",', '"Afficher l\'icône",',
                     '"Couleur de texte propre",', '"Pokaż ikonę",', '"Własny kolor tekstu",']) {
  assert.ok(i18n.includes(label), label);
}

// Browser: the real collect/load/preview functions on a small DOM stand-in.
const snapshots = read('src/web/admin/tiles/snapshots.js');
const layout = read('src/web/admin/tiles/layout.js');
const pick = (source, name) => {
  const start = source.indexOf(`function ${name}(`);
  assert.ok(start >= 0, name);
  let depth = 0;
  for (let i = source.indexOf('{', start); i < source.length; ++i) {
    if (source[i] === '{') depth++;
    else if (source[i] === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  throw new Error(name);
};
class ClassList {
  constructor() { this.set = new Set(); }
  toggle(name, on) { if (on === undefined) on = !this.set.has(name); on ? this.set.add(name) : this.set.delete(name); return on; }
  add(...names) { names.forEach(n => this.set.add(n)); }
  contains(name) { return this.set.has(name); }
}
const element = extra => Object.assign({value: '', checked: false, disabled: false, textContent: '',
  dataset: {}, classList: new ClassList(), style: {opacity: '', props: {},
    setProperty(k, v) { this.props[k] = v; }, removeProperty(k) { delete this.props[k]; }}}, extra);
const dom = {
  screensaver_tile_ss_icon: element({checked: true}),
  screensaver_tile_ss_opacity: element({value: '255'}),
  screensaver_tile_ss_opacity_value: element(),
  screensaver_tile_ss_text_color_on: element({checked: false}),
  screensaver_tile_ss_text_color: element({value: '#ffffff'}),
  screensaver_tile_ss_text_align: element({value: '0'}),
};
const context = {document: {getElementById: id => dom[id] || null}};
vm.createContext(context);
vm.runInContext([pick(layout, 'clampInt'), pick(snapshots, 'collectScreensaverLookFields'),
  pick(snapshots, 'loadScreensaverLookFields'), pick(snapshots, 'resetScreensaverLookFields'),
  pick(snapshots, 'syncScreensaverLookFields'),
  pick(snapshots, 'applyScreensaverLookPreview'),
  'this.api = {collectScreensaverLookFields, loadScreensaverLookFields, resetScreensaverLookFields, applyScreensaverLookPreview};'
].join('\n'), context);
const api = context.api;

assert.deepEqual({...api.collectScreensaverLookFields('screensaver', '1')},
  {ss_icon: '1', ss_opacity: '255', ss_text_color: '', ss_text_align: '0'}, 'defaults');
assert.deepEqual({...api.collectScreensaverLookFields('screensaver', '0')}, {}, 'an empty tile has no look');
assert.deepEqual({...api.collectScreensaverLookFields('folder0', '1')}, {}, 'normal grids have no look');

api.loadScreensaverLookFields('screensaver', {ss_icon: 0, ss_opacity: 128, ss_text_color: '#00FF00', ss_text_align: 1});
assert.equal(dom.screensaver_tile_ss_icon.checked, false);
assert.equal(dom.screensaver_tile_ss_opacity_value.textContent, '50 %');
assert.equal(dom.screensaver_tile_ss_text_color.disabled, false);
const saved = {...api.collectScreensaverLookFields('screensaver', '1')};
assert.deepEqual(saved, {ss_icon: '0', ss_opacity: '128', ss_text_color: '#00FF00', ss_text_align: '1'}, 'round trip');

const tile = element({dataset: {type: '1'}});
api.applyScreensaverLookPreview(tile, saved);
assert.ok(tile.classList.contains('ss-icon-hidden') && tile.classList.contains('ss-text-colored'));
assert.equal(tile.style.opacity, '0.502');
assert.equal(tile.style.props['--ss-text-color'], '#00FF00');
assert.ok(tile.classList.contains('ss-text-centered'), 'centered text position');

api.resetScreensaverLookFields('screensaver');
assert.deepEqual({...api.collectScreensaverLookFields('screensaver', '1')},
  {ss_icon: '1', ss_opacity: '255', ss_text_color: '', ss_text_align: '0'}, 'reset');
assert.equal(dom.screensaver_tile_ss_text_color.disabled, true);
const empty = element({dataset: {type: '0'}});
empty.style.opacity = '0.5';
api.applyScreensaverLookPreview(empty, {});
assert.equal(empty.style.opacity, '', 'an emptied slot drops the old opacity');

// Every path carries the look: editor bindings, drafts, snapshots, both
// previews and the import.
assert.match(snapshots, /'icon_disc', 'icon_glow', 'ss_opacity', 'ss_icon', 'ss_text_align'\];/);
assert.match(snapshots, /syncIconDiscFields\(tab\);\n\s*loadScreensaverLookFields\(tab, data\);/);
assert.match(snapshots, /collectScreensaverLookFields\(prefix, typeValue\)\);/);
assert.match(read('src/web/admin/tiles/editor.js'), /\['_tile_ss_text_color', 'input', 'tileSsTextColor'\]/);
assert.match(read('src/web/admin/tiles/import-export.js'), /for \(const key of \['ss_opacity', 'ss_fill', 'ss_icon', 'ss_text_color', 'ss_text_align'\]\)/);
assert.match(read('src/web/assets/admin.css'), /\.screensaver-tile-grid > \.tile\.ss-icon-hidden \.tile-icon \{ visibility:hidden; \}/);
console.log('Screensaver tile look: per-tile opacity, fill, icon and text color stored, served, drawn and previewed');
