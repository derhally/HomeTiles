// Climate temperature/humidity sensors (Zeid 2026-10-09): a Climate tile can
// show a chosen sensor instead of the Climate entity's own (often estimated)
// current temperature and humidity. Runs the real delivered Web Admin preview,
// save, load and reset paths, and checks the firmware storage, MQTT routing,
// runtime override and translations that carry the same field names.
import {readAdminDeliverySource} from '../../lib/admin-source.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const noop = () => {};
const classList = {
  add: noop,
  remove: noop,
  toggle: noop,
  contains: () => false
};

class TestElement {
  constructor(value = '') {
    this.value = value;
    this.dataset = {};
    this.listeners = {};
    this.attributes = {};
    this.options = [];
    this.selectedOptions = [];
    this.classList = classList;
    this.style = { removeProperty: noop, setProperty: noop };
    this.className = '';
    this.innerHTML = '';
  }

  setAttribute(name, value) {
    this.attributes[name] = String(value);
  }

  removeAttribute(name) {
    delete this.attributes[name];
  }

  getAttribute(name) {
    return Object.prototype.hasOwnProperty.call(this.attributes, name)
      ? this.attributes[name]
      : null;
  }

  addEventListener(name, handler) {
    (this.listeners[name] ??= []).push(handler);
  }

  removeEventListener(name, handler) {
    this.listeners[name] = (this.listeners[name] || [])
      .filter(candidate => candidate !== handler);
  }

  appendChild(option) {
    this.options.push(option);
  }

  querySelector() {
    return null;
  }
}

const elements = {};
const storage = {
  getItem: () => null,
  setItem: noop,
  removeItem: noop,
  key: () => null,
  length: 0
};
const document = {
  documentElement: { lang: 'en', dataset: {}, addEventListener: noop },
  addEventListener: noop,
  getElementById: id => elements[id] || null,
  querySelector: () => null,
  querySelectorAll: () => [],
  body: { classList, dataset: {} },
  createElement: () => new TestElement()
};

const sandbox = {
  console,
  document,
  localStorage: storage,
  sessionStorage: storage,
  setTimeout: () => 0,
  clearTimeout: noop,
  setInterval: () => 0,
  clearInterval: noop,
  requestAnimationFrame: noop,
  addEventListener: noop,
  fetch: () => Promise.reject(new Error('unexpected fetch')),
  FormData,
  URLSearchParams,
  Intl,
  Date,
  Math,
  JSON,
  Number,
  String,
  Object,
  Array,
  Map,
  Set,
  Promise,
  parseInt,
  parseFloat,
  isNaN,
  isFinite,
  APP_I18N: {},
  CLIMATE_I18N: {
    automatic: 'Automatic',
    empty: 'Empty',
    emptyRemove: 'Remove',
    emptyField: 'Empty field',
    selectedField: 'Selected field',
    horizontal: 'Horizontal',
    vertical: 'Vertical',
    currentTemperature: 'Current',
    currentHumidity: 'Current humidity',
    targetTemperature: 'Target temperature',
    targetHumidity: 'Target humidity',
    mode: 'Mode',
    off: 'Off',
    heat: 'Heat',
    cool: 'Cool',
    heatCool: 'Heat/Cool',
    autoMode: 'Auto',
    dry: 'Dry',
    fanOnly: 'Fan only',
    genericClimate: 'Climate',
    unavailable: 'Unavailable',
    unknown: 'Unknown'
  },
  GRID_COLS: 6,
  GRID_ROWS: 4,
  TILES_PER_GRID: 24,
  ADMIN_WEB_SESSION_TOKEN: 'test',
  TILE_TYPE_REGISTRY: {
    17: {
      fields: 'climate',
      preview: 'climate',
      load: 'loadClimateFields',
      save: 'saveClimateFields',
      reset: 'resetClimateFields'
    }
  },
  TILE_TABS: [],
  TAB_BY_FOLDER: {},
  FOLDER_BY_TAB: { folder1: 1 },
  SCREENSAVER_FOLDER_ID: 65535,
  SCREENSAVER_TILE_DEFAULT_OPACITY: 0,
  SCREENSAVER_TILE_DEFAULT_COLOR: '#000000',
  MEDIA_TILE_TYPE: 15,
  MEDIA_TILE_MIN_SPAN: 1,
  MEDIA_TILE_MAX_SPAN: 6,
  APP_LOCALE: 'en',
  navigator: {},
  location: {},
  confirm: () => true
};
sandbox.window = sandbox;

vm.createContext(sandbox);
vm.runInContext(
  readAdminDeliverySource(),
  sandbox,
  { filename: 'admin.js' }
);


const readSource = file => fs.readFileSync(
  new URL('../../../' + file, import.meta.url), 'utf8').replace(/\r\n?/g, '\n');

// Override helper: sensor values replace the Climate values, a missing or
// non-numeric sensor state shows "--" instead of the Climate value.
const overrides = vm.runInContext(`(() => {
  const payload = JSON.stringify({
    state: 'heat', attributes: { current_temperature: 19, current_humidity: 40, temperature: 21 }
  });
  const values = { 'sensor.room_t': '22.46', 'sensor.room_h': '55,4', 'sensor.dead': 'unavailable' };
  return {
    none: applyClimateSensorOverrides(parseClimatePreviewPayload(payload), '', '', values),
    both: applyClimateSensorOverrides(parseClimatePreviewPayload(payload), 'sensor.room_t', 'sensor.room_h', values),
    dead: applyClimateSensorOverrides(parseClimatePreviewPayload(payload), 'sensor.dead', 'sensor.missing', values),
    invalid: applyClimateSensorOverrides(parseClimatePreviewPayload(''), 'sensor.room_t', '', values)
  };
})()`, sandbox);
assert.equal(overrides.none.current, '19.0');
assert.equal(overrides.none.currentHumidity, '40');
assert.equal(overrides.both.current, '22.5');
assert.equal(overrides.both.currentHumidity, '55');
assert.equal(overrides.both.target, '21.0', 'targets stay with the Climate entity');
assert.equal(overrides.dead.current, '--');
assert.equal(overrides.dead.currentHumidity, null);
assert.equal(overrides.invalid.valid, false, 'a missing Climate state stays invalid');

for (const [id, value] of Object.entries({
  folder1_tile_title: 'Living room',
  folder1_tile_icon: '',
  folder1_tile_color: '#2A2A2A',
  folder1_tile_col: '1',
  folder1_tile_row: '1',
  folder1_tile_span_w: '2',
  folder1_tile_span_h: '1',
  folder1_tile_type: '17',
  folder1_climate_entity: 'climate.test',
  folder1_climate_temperature_sensor: 'sensor.room_t',
  folder1_climate_humidity_sensor: 'sensor.room_h',
  folder1_climate_popup_open_mode: '1',
  folder1_climate_view: '0',
  folder1_climate_geometry: ''
})) {
  elements[id] = new TestElement(value);
}
for (let index = 0; index < 6; ++index) {
  elements[`folder1_climate_slot_${index}`] = new TestElement('0');
  elements[`folder1_climate_layout_${index}`] = new TestElement('0');
}
elements['folder1-tile-3'] = new TestElement();
elements['folder1-tile-3'].dataset.type = '17';

// Real live preview: an explicit current temperature slot shows the sensor.
vm.runInContext(`
  currentTileIndex = 3;
  currentTileTab = 'folder1';
  tilesData.folder1 = [];
  sensorMetaCache.values['climate.test'] = JSON.stringify({
    state: 'heat', supported_features: 1,
    attributes: { current_temperature: 19.1, current_humidity: 40, temperature: 21 }
  });
  sensorMetaCache.values['sensor.room_t'] = '23.4';
  sensorMetaCache.values['sensor.room_h'] = '61';
  updateTilePreview('folder1');
`, sandbox);
const preview = elements['folder1-tile-3'].innerHTML;
assert.ok(preview.includes('23.4'), 'live preview shows the temperature sensor');
assert.ok(!preview.includes('19.1'), 'live preview hides the estimated Climate temperature');

// The editor offers the current humidity field once a humidity sensor exists.
const kinds = vm.runInContext(`(() => {
  sensorMetaCache.values['climate.test'] = JSON.stringify({
    state: 'heat', attributes: { current_temperature: 19.1 }
  });
  const withSensor = Array.from(climateAvailableContentKinds('folder1') || []);
  document.getElementById('folder1_climate_humidity_sensor').value = '';
  const without = Array.from(climateAvailableContentKinds('folder1') || []);
  document.getElementById('folder1_climate_humidity_sensor').value = 'sensor.room_h';
  return { withSensor, without, humidity: CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY };
})()`, sandbox);
assert.ok(kinds.withSensor.includes(kinds.humidity));
assert.ok(!kinds.without.includes(kinds.humidity));

// Save -> snapshot/clipboard -> load -> reset use the same field names.
const roundtrip = JSON.parse(vm.runInContext(`JSON.stringify((() => {
  const fd = new FormData();
  saveClimateFields('folder1', fd);
  const saved = Object.fromEntries(fd.entries());
  resetClimateFields('folder1');
  const reset = [document.getElementById('folder1_climate_temperature_sensor').value,
                 document.getElementById('folder1_climate_humidity_sensor').value];
  loadClimateFields('folder1', Object.assign({ sensor_entity: 'climate.test' }, saved));
  const loaded = [document.getElementById('folder1_climate_temperature_sensor').value,
                  document.getElementById('folder1_climate_humidity_sensor').value];
  loadClimateFields('folder1', { sensor_entity: 'climate.test' });
  const cleared = [document.getElementById('folder1_climate_temperature_sensor').value,
                   document.getElementById('folder1_climate_humidity_sensor').value];
  return { saved, reset, loaded, cleared };
})())`, sandbox));
assert.equal(roundtrip.saved.climate_temperature_sensor, 'sensor.room_t');
assert.equal(roundtrip.saved.climate_humidity_sensor, 'sensor.room_h');
assert.deepEqual(roundtrip.reset, ['', '']);
assert.deepEqual(roundtrip.loaded, ['sensor.room_t', 'sensor.room_h']);
assert.deepEqual(roundtrip.cleared, ['', '']);

// Editor bindings, option lists, import and the cached grid preview.
const delivered = readAdminDeliverySource();
for (const name of ['climateTemperatureSensor', 'climateHumiditySensor']) {
  assert.ok(readSource('src/web/admin/tiles/editor.js').includes(`'${name}'`), `editor binds ${name}`);
}
assert.match(readSource('src/web/admin/tiles/editor.js'),
  /syncClimateSlotFields\(tab\); updateTilePreview\(tab\); updateDraft\(tab\); scheduleAutoSave\(tab\);\n      \}\);\n    \}/);
for (const field of ['climate_temperature_sensor', 'climate_humidity_sensor']) {
  assert.ok(readSource('src/web/admin/tiles/registry.js').includes(`rebuildEntitySelect(tab + '_${field}', data.sensors);`));
  assert.ok(readSource('src/web/admin/tiles/import-export.js').includes(`fd.append('${field}', tile.${field} || '');`));
  assert.ok(delivered.includes(field), `delivered admin.js carries ${field}`);
}
assert.ok(readSource('src/web/admin/tiles/grid-preview.js').includes('tile.climate_temperature_sensor ||'));

// Firmware: request fields, JSON, sidecar storage, routes, runtime override.
const handler = readSource('src/types/climate/web_handler.cpp');
assert.ok(handler.includes('server.hasArg("climate_temperature_sensor")'));
assert.ok(handler.includes('server.hasArg("climate_humidity_sensor")'));
assert.ok(handler.includes('normalizeClimateSensors(tile);'));
const tiles = readSource('src/web/server/handlers/web_admin_tiles.cpp');
assert.ok(tiles.includes('\\"climate_temperature_sensor\\":'));
assert.ok(tiles.includes('const String sensors = climateSensorsRecord(tile);'));
const config = readSource('src/tiles/config/tile_config.cpp');
for (const needle of [
  'static const char* kClimateSensorPathDir = "/_tile_climate_sensors";',
  'scanSidecarDir(kClimateSensorPathDir, g_climate_sensor_sidecar_keys);',
  'if (&keys == &g_climate_sensor_sidecar_keys) return g_climate_sensor_sidecar_texts;',
  'applyClimateSensorsFromSd(folder_id, grid);',
  'writeClimateSensorsSd(id, i, "");',
  'writeClimateSensorsSd(folder_id, grid_idx, climateSensorsRecord(tile))',
  'normalizeClimateSensors(working.tiles[i]);',
  'e->extra_entities[i] = psramStrdupLocal(slots[i].extra_entities);'
]) {
  assert.ok(config.includes(needle), `tile_config.cpp: ${needle}`);
}
const header = readSource('src/tiles/config/tile_config.h');
assert.ok(header.includes('a.climate_temperature_sensor == b.climate_temperature_sensor'));
assert.ok(readSource('src/network/mqtt/mqtt_handlers.cpp').includes('for (const char* extra = slot.extra_entities; *extra;)'));
const renderer = readSource('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('apply_climate_sensor_overrides(sensor_tile, &state, sensor_hash);'));
assert.ok(renderer.includes('apply_climate_sensor_overrides(sensor_tile, nullptr, payload_hash);'));
assert.ok(readSource('src/ui/tabs/tiles/tab_tiles_unified.cpp').includes(
  'tile.climate_temperature_sensor.equalsIgnoreCase(entity_id)'));

// Every language has the three labels next to its Climate entity label.
const i18n = readSource('src/core/i18n/i18n.cpp');
const locales = [...i18n.matchAll(/static const LocaleProfile kLocale(\w+) = \{/g)].map(match => match[1]);
assert.ok(locales.length >= 4);
for (const locale of locales) {
  const start = i18n.indexOf(`static const LocaleProfile kLocale${locale} = {`);
  const block = i18n.slice(start, start + 2000);
  assert.match(block, /\n    "[^"\n]+",\n    \{"[^"\n]+", "[^"\n]+", "[^"\n]+"\},\n    "[^"\n]+",\n/,
    `${locale}: climate_sensor_labels follow the Climate entity label`);
}
assert.ok(readSource('src/types/climate/web_html.cpp').includes('i18n::climate_sensor_label(language, field)'));

console.log('Climate temperature/humidity sensors: preview, editor, storage, routing and translations');
