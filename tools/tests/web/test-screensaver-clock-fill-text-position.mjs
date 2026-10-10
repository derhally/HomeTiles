// Screensaver readability (user 2026-10-10): the clock was unreadable on a
// light image and a sensor without a title showed its value one line below
// the icon. The clock gets an optional card chosen like a tile color (None,
// Global, Custom) with an opacity, and every
// screensaver tile a Text position: "Centered on icon" centers a value
// without a title on the icon row. Panel, Web Admin preview, storage,
// endpoint and translations must agree.
import assert from 'node:assert/strict';
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = p => readRepoFile(p).replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

// Panel: a lone value is centered on the disc row only when asked.
const compact = read('src/tiles/runtime/compact_sensor_layout.h');
assert.match(compact, /inline int value_only_top\(uint8_t value_choice = 0\) \{\n\s*return std::max<int>\(0, \(header_height\(\) - value_font\(value_choice\)->line_height\) \/ 2\);/);
assert.match(compact, /if \(center_text && !title && value\) \{\n\s*text\(value, value_font\(value_choice\), value_only_top\(value_choice\)\);\n\s*return;/);
assert.match(compact, /tile\.sensor_value_font, tile\.ss_text_align == 1\);/);

// Storage: only a non-standard position is written; junk reads as standard.
const look = read('src/ui/screensaver/screensaver_tile_look.h');
assert.match(look, /return raw == kTextAlignCentered \? kTextAlignCentered : kTextAlignStandard;/);
assert.match(look, /tile\.ss_text_align == kTextAlignStandard;/);
assert.match(look, /tile\.ss_text_align = kTextAlignStandard;/);
const config = read('src/ui/screensaver/screensaver_config.cpp');
assert.match(config, /if \(tile\.ss_text_align != screensaver_tile_look::kTextAlignStandard\) item\["align"\] = tile\.ss_text_align;/);
assert.match(config, /tile\.ss_text_align = screensaver_tile_look::normalize_text_align\(item\["align"\] \| 0\);/);
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /normalize_text_align\(server\.arg\("ss_text_align"\)\.toInt\(\)\)/);

// Clock fill: stored with the clock settings, off by default, drawn as a card
// with the shared padding and radius.
const header = read('src/ui/screensaver/screensaver_config.h');
assert.match(header, /uint8_t clock_fill_mode = kScreensaverClockFillNone;\n\s*uint32_t clock_fill_color = 0x000000;\n\s*uint8_t clock_fill_opacity = 128;/);
// The first version stored clock_fill (on = own color).
assert.match(fn(config, 'ScreensaverConfigStore::loadPath'), /\(doc\["clock_fill"\] \| false\) \? kScreensaverClockFillCustom\s*: kScreensaverClockFillNone/);
assert.match(fn(config, 'ScreensaverConfigStore::loadPath'), /constrain\(doc\["clock_fill_opacity"\] \| 128, 0, 255\)/);
const toJson = config.slice(config.indexOf('String ScreensaverConfigStore::toJson('),
                            config.indexOf('bool ScreensaverConfigStore::save()'));
for (const key of ['clock_fill_mode', 'clock_fill_color', 'clock_fill_opacity']) assert.ok(toJson.includes(`doc["${key}"]`), key);
const clock = fn(read('src/ui/screensaver/image_screensaver.cpp'), 'rebuild_global_clock');
assert.match(clock, /if \(config\.clock_fill_mode != kScreensaverClockFillNone\) \{[\s\S]*\? tileDefaultBgColor\(\)[\s\S]*lv_obj_set_style_bg_opa\(st->clock_box, config\.clock_fill_opacity, 0\);[\s\S]*screensaver_clock_fill::pad_x\(\)/);
const styles = read('src/web/server/render/web_admin_styles.cpp');
for (const name of ['screensaver-clock-fill-pad-x', 'screensaver-clock-fill-pad-y', 'screensaver-clock-fill-radius',
                    'compact-value-only-top', 'compact-value-only-top-24', 'compact-value-only-top-28']) {
  assert.ok(styles.includes(`"${name}"`), name);
}

// Translations in every language; the HTML takes them from the schema.
const i18nHeader = read('src/core/i18n/i18n.h');
for (const key of ['screensaver_tile_text_position', 'screensaver_text_position_standard', 'screensaver_text_position_centered']) {
  assert.ok(i18nHeader.includes(`const char* ${key};`), key);
}
const i18n = read('src/core/i18n/i18n.cpp');
for (const label of ['"Text position",', '"Centered on icon",', '"Textposition",', '"Am Icon zentriert",',
                     '"Position du texte",', '"Centré sur l\'icône",', '"Pozycja tekstu",', '"Wyśrodkowany przy ikonie",']) {
  assert.ok(i18n.includes(label), label);
}
const html = read('src/web/server/render/web_admin_html.cpp');
for (const needle of ['tr.screensaver_tile_text_position', 'tr.screensaver_text_position_centered', '_tile_ss_text_align',
                      'id="screensaverClockFillModes"', 'id="screensaverClockFillColor"', 'id="screensaverClockFillOpacity"',
                      'cssClass += " ss-text-centered";']) {
  assert.ok(html.includes(needle), needle);
}
assert.match(read('src/web/admin/tiles/import-export.js'), /clock_fill_opacity: Math\.round\(ssClamp\(data\?\.clock_fill_opacity \?\? 128, 0, 255\)\)/);

// Browser: the real editor draws the clock card and the CSS centers a lone
// value only on a centered tile.
const inputs = ['screensaverUseWallpapers', 'screensaverShuffle', 'screensaverTileShadow', 'screensaverTileBorder',
  'screensaverShowTime', 'screensaverShowDate', 'screensaverShowWeekday', 'screensaverClockShadow']
  .map(id => `<input type="checkbox" id="${id}">`).join('') +
  ['screensaverTimeFont', 'screensaverDateFont', 'screensaverTimeAlignment', 'screensaverDateAlignment',
   'screensaverTimeFormat', 'screensaverDateFormat', 'screensaverWallpaperDuration', 'screensaverWallpaperZoom',
   'screensaverFocusX', 'screensaverFocusY'].map(id => `<input id="${id}">`).join('') +
  '<div id="screensaverClockFillModes"><button data-clock-fill-mode="0"></button><button data-clock-fill-mode="1"></button><button data-clock-fill-mode="2"></button></div>' +
  '<input type="color" id="screensaverClockFillColor"><label id="screensaverClockFillOpacityRow"><input type="range" min="0" max="255" id="screensaverClockFillOpacity"><output id="screensaverClockFillOpacityValue"></output></label>';

const page = `<!doctype html><html lang="en"><head><style>:root {--grid-cols:7;--grid-rows:5;--preview-cell-w:85px;--preview-cell-h:73px;--preview-gap:10px;--preview-pad:12px;--screensaver-image-inset:10px;--tile-radius:16px;--screensaver-fs48:48px;--screensaver-lh48:58px;--screensaver-ldy48:0px;--screensaver-fs28:28px;--screensaver-lh28:34px;--screensaver-ldy28:0px;--screensaver-clock-gap:6px;--screensaver-clock-fill-pad-x:16px;--screensaver-clock-fill-pad-y:8px;--screensaver-clock-fill-radius:16px;--compact-h:40px;--compact-inset:6px;--compact-title-line:24px;--compact-value-line:24px;--compact-value-dy:0px;--compact-title-top:4px;--compact-value-top:28px;--compact-value-only-top:8px;--compact-value-only-top-24:7px;--compact-value-top-24:29px;--compact-title-top-24:3px;--compact-value-line-step-24:29px;} ${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-screensaver" class="tile-tab tab-content active"><div id="screensaverGrid" class="tile-grid screensaver-tile-grid"><div class="screensaver-grid-image-frame"><img id="screensaverPreviewImage" hidden></div>
<div id="screensaverClock" class="screensaver-grid-clock"><div id="screensaverClockTime">--:--</div><div id="screensaverClockDate">--.--.----</div><span class="screensaver-clock-resize-handle"></span></div></div></div>
${inputs}<div id="screensaverWallpaperControls"></div><div id="screensaverWallpaperList"></div>
<div class="tile sensor-compact ss-text-centered" id="centered" style="position:relative;width:200px;height:40px"><div class="tile-value">69.9 °F</div></div>
<div class="tile sensor-compact ss-text-centered compact-value-24" id="centered24" style="position:relative;width:200px;height:40px"><div class="tile-value">69.9 °F</div></div>
<div class="tile sensor-compact ss-text-centered" id="titled" style="position:relative;width:200px;height:40px"><div class="tile-title">Outside</div><div class="tile-value">69.9 °F</div></div>
<div class="tile sensor-compact" id="standard" style="position:relative;width:200px;height:40px"><div class="tile-value">69.9 °F</div></div>
<pre id="result"></pre><script>
const nativeListen=document.addEventListener.bind(document);
document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={},TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3,MEDIA_TILE_TYPE=15;
window.fetch=async()=>({json:async()=>({success:true})});
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const grid=document.getElementById('screensaverGrid');
 grid.style.width='679px';grid.style.height='429px';
 const frame=grid.querySelector('.screensaver-grid-image-frame').getBoundingClientRect();
 const scale=frame.width/1280;
 const clock=document.getElementById('screensaverClock');
 const center=()=>{const r=clock.getBoundingClientRect();return {x:r.left+r.width/2,y:r.top+r.height/2};};
 screensaverDraft=ssNormalizeLoaded({screen_width:1280,screen_height:800,clock_x:500,clock_y:500,show_time:true,show_date:true,show_weekday:false,
   time_font_size:48,date_font_size:28,time_alignment:1,date_alignment:1,time_format:1,date_format:0,
   clock_shadow:true,tile_shadow:false,tile_border:true,use_wallpapers:false,shuffle:false,duration_seconds:60,wallpapers:[]});
 check(screensaverDraft.clock_fill_mode===0&&screensaverDraft.clock_fill_color==='#000000'&&screensaverDraft.clock_fill_opacity===128,'Defaults: None, black, 50 %');
 check(ssNormalizeLoaded({clock_fill:true}).clock_fill_mode===2,'The first clock fill (own color) loads as Custom');
 renderScreensaverEditor();
 check(getComputedStyle(clock).backgroundColor==='rgba(0, 0, 0, 0)','No card without fill');
 check(document.getElementById('screensaverClockFillColor').hidden&&document.getElementById('screensaverClockFillOpacityRow').hidden,'None shows neither color nor opacity');
 check(document.querySelector('[data-clock-fill-mode="0"]').classList.contains('active'),'None is selected');
 const before=center();
 screensaverDraft.clock_fill_mode=2;screensaverDraft.clock_fill_color='#336699';screensaverDraft.clock_fill_opacity=255;
 renderScreensaverEditor();
 const cs=getComputedStyle(clock);
 check(cs.backgroundColor==='rgb(51, 102, 153)','Fill color: '+cs.backgroundColor);
 check(Math.abs(parseFloat(cs.paddingLeft)-16*scale)<0.01&&Math.abs(parseFloat(cs.paddingTop)-8*scale)<0.01,'Card padding scales with the frame: '+cs.paddingLeft+' '+cs.paddingTop);
 const after=center();
 check(Math.abs(after.x-before.x)<0.6&&Math.abs(after.y-before.y)<0.6,'The card grows around the same center');
 check(!document.getElementById('screensaverClockFillColor').hidden&&!document.getElementById('screensaverClockFillOpacityRow').hidden,'Custom shows color and opacity');
 document.documentElement.style.setProperty('--tile-default-bg','#204060');
 screensaverDraft.clock_fill_mode=1;renderScreensaverEditor();
 check(getComputedStyle(clock).backgroundColor==='rgb(32, 64, 96)','Global uses the global tile color: '+getComputedStyle(clock).backgroundColor);
 check(document.getElementById('screensaverClockFillColor').hidden,'Global hides the own color');
 screensaverDraft.clock_fill_mode=2;
 screensaverDraft.clock_fill_opacity=128;renderScreensaverEditor();
 check(document.getElementById('screensaverClockFillOpacityValue').textContent==='50 %','Opacity label');
 check(ssPayload(screensaverDraft).clock_fill_mode===2&&ssPayload(screensaverDraft).clock_fill_color==='#336699','Saved with the clock settings');
 screensaverDraft.clock_fill_mode=0;renderScreensaverEditor();
 check(clock.style.background===''&&clock.style.padding==='','Turning the fill off removes the card');
 const top=id=>document.querySelector('#'+id+' > .tile-value').offsetTop;
 check(top('centered')===8,'Lone value centered on the icon row: '+top('centered'));
 check(top('centered24')===7,'Centered top follows the value size: '+top('centered24'));
 check(top('titled')===28,'With a title the block stays as before: '+top('titled'));
 check(top('standard')===28,'Standard keeps the title line: '+top('standard'));
 document.body.dataset.result='pass';document.getElementById('result').textContent='Screensaver clock fill and text position';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
try {
  if (runDomHarness({label: 'Screensaver clock fill and text position', html: page,
                     tmpPrefix: 'hometiles-screensaver-fill-', extraArgs: ['--virtual-time-budget=2000', '--window-size=1280,1000']})) {
    console.log('Screensaver clock fill and text position: stored, served, drawn and previewed');
  }
} catch (error) { throw new Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 400)); }
