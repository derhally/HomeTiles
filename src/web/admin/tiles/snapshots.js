
  // Per-tile icon disc options are common to every type with an icon, so
  // they travel with the type fields through drafts, copy/paste and saves.
  function tileTypeHasIcon(typeValue) {
    return !['0', '16'].includes(String(typeValue ?? '0'));
  }
  // Glow only matters where the icon can take a color: from its entity
  // (switch/light, climate, cover, binary sensor), from the color bar or
  // state colors (sensor family, energy) or from a fixed icon color (scene,
  // folder, back, camera). Other icons are always white.
  function tileTypeHasColoredIcon(typeValue) {
    return ['1', '2', '4', '5', '8', '12', '14', '15', '17', '18', '19', '20', '21', '22', '23']
      .includes(String(typeValue ?? '0'));
  }
  // Stored disc mode: 0 follows the global option, 2 hides the disc on this
  // tile. A legacy stored 1 ("on") loads as checked.
  function iconDiscModeFromCheckbox(box) {
    return box?.checked === false ? '2' : '0';
  }
  // Like the per-tile Tile borders option, only Back, Clock and Text can hide
  // their own icon disc; every other tile follows the global option.
  function tileTypeHasDiscToggle(typeValue) {
    return ['8', '9', '10'].includes(String(typeValue ?? '0'));
  }
  function collectIconDiscFields(tab, typeValue) {
    const box = document.getElementById(tab + '_tile_icon_disc');
    if (!box || !tileTypeHasIcon(typeValue)) return {};
    const glow = document.getElementById(tab + '_tile_icon_glow');
    return {
      icon_disc: tileTypeHasDiscToggle(typeValue) ? iconDiscModeFromCheckbox(box) : '0',
      icon_glow: tileTypeHasColoredIcon(typeValue) && glow?.checked === false ? '0' : '1',
    };
  }
  function loadIconDiscFields(tab, data) {
    const box = document.getElementById(tab + '_tile_icon_disc');
    if (box) box.checked = String(data?.icon_disc) !== '2';
    const glow = document.getElementById(tab + '_tile_icon_glow');
    if (glow) glow.checked = !['0', 'false'].includes(String(data?.icon_glow));
    syncIconDiscFields(tab);
    loadScreensaverLookFields(tab, data);
  }
  function resetIconDiscFields(tab) {
    const box = document.getElementById(tab + '_tile_icon_disc');
    if (box) box.checked = true;
    const glow = document.getElementById(tab + '_tile_icon_glow');
    if (glow) glow.checked = true;
    resetScreensaverLookFields(tab);
  }
  function syncIconDiscFields(tab) {
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const discToggle = tileTypeHasDiscToggle(typeValue);
    const colored = tileTypeHasColoredIcon(typeValue);
    const weather = typeValue === '12';
    document.getElementById(tab + '_tile_icon_disc_fields')
      ?.classList.toggle('hidden', !tileTypeHasIcon(typeValue) || (!discToggle && !colored && !weather));
    document.getElementById(tab + '_tile_icon_disc_row')?.classList.toggle('hidden', !discToggle);
    document.getElementById(tab + '_tile_icon_glow_row')?.classList.toggle('hidden', !colored);
    document.getElementById(tab + '_weather_colored_icons_row')?.classList.toggle('hidden', !weather);
  }

  // Per-tile screensaver look (screensaver_tile_look.h): whole-card opacity,
  // background fill, icon visibility and an own text color. Only the
  // screensaver tile editor has these fields.
  function collectScreensaverLookFields(tab, typeValue) {
    const fill = document.getElementById(tab + '_tile_ss_fill');
    if (!fill || String(typeValue ?? '0') === '0') return {};
    const textOn = document.getElementById(tab + '_tile_ss_text_color_on')?.checked === true;
    return {
      ss_fill: fill.checked ? '1' : '0',
      ss_icon: document.getElementById(tab + '_tile_ss_icon')?.checked === false ? '0' : '1',
      ss_opacity: String(clampInt(document.getElementById(tab + '_tile_ss_opacity')?.value, 0, 255, 255)),
      ss_text_color: textOn
        ? String(document.getElementById(tab + '_tile_ss_text_color')?.value || '#FFFFFF').toUpperCase()
        : '',
    };
  }
  function loadScreensaverLookFields(tab, data) {
    const fill = document.getElementById(tab + '_tile_ss_fill');
    if (!fill) return;
    fill.checked = !['0', 'false'].includes(String(data?.ss_fill));
    const icon = document.getElementById(tab + '_tile_ss_icon');
    if (icon) icon.checked = !['0', 'false'].includes(String(data?.ss_icon));
    const opacity = document.getElementById(tab + '_tile_ss_opacity');
    if (opacity) opacity.value = String(clampInt(data?.ss_opacity, 0, 255, 255));
    const text = /^#[0-9a-f]{6}$/i.test(String(data?.ss_text_color || '')) ? String(data.ss_text_color) : '';
    const textOn = document.getElementById(tab + '_tile_ss_text_color_on');
    if (textOn) textOn.checked = !!text;
    const color = document.getElementById(tab + '_tile_ss_text_color');
    if (color && text) color.value = text.toLowerCase();
    syncScreensaverLookFields(tab);
  }
  function resetScreensaverLookFields(tab) {
    loadScreensaverLookFields(tab, {});
    const color = document.getElementById(tab + '_tile_ss_text_color');
    if (color) color.value = '#ffffff';
  }
  function syncScreensaverLookFields(tab) {
    const opacity = document.getElementById(tab + '_tile_ss_opacity');
    const output = document.getElementById(tab + '_tile_ss_opacity_value');
    if (opacity && output) output.textContent = Math.round(Number(opacity.value) * 100 / 255) + ' %';
    const color = document.getElementById(tab + '_tile_ss_text_color');
    if (color) color.disabled = document.getElementById(tab + '_tile_ss_text_color_on')?.checked !== true;
  }
  // A tile without fill draws its background at opacity 0, like the panel
  // (apply_slot_look); tints keep that through data-bg-opacity.
  function screensaverLookFilled(look) {
    return !['0', 'false'].includes(String(look?.ss_fill));
  }
  // The rest of the look as the screensaver tile previews show it (live and
  // cached grid): no outline without fill, hidden icon, whole-card opacity,
  // own text color.
  function applyScreensaverLookPreview(tileElem, look) {
    if (!tileElem) return;
    const empty = String(tileElem.dataset.type || '0') === '0';
    const fill = empty || screensaverLookFilled(look);
    const icon = empty || !['0', 'false'].includes(String(look?.ss_icon));
    const opacity = empty ? 255 : clampInt(look?.ss_opacity, 0, 255, 255);
    const text = !empty && /^#[0-9a-f]{6}$/i.test(String(look?.ss_text_color || ''))
      ? String(look.ss_text_color) : '';
    tileElem.classList.toggle('ss-no-fill', !fill);
    if (!fill) tileElem.classList.add('tile-border-hidden');
    tileElem.classList.toggle('ss-icon-hidden', !icon);
    tileElem.style.opacity = opacity < 255 ? (opacity / 255).toFixed(3) : '';
    tileElem.classList.toggle('ss-text-colored', !!text);
    if (text) tileElem.style.setProperty('--ss-text-color', text);
    else tileElem.style.removeProperty('--ss-text-color');
  }

  function collectTypeFieldValues(tab) {
    const prefix = tab;
    const typeValue = document.getElementById(prefix + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    const out = Object.assign(collectIconDiscFields(prefix, typeValue),
                              collectScreensaverLookFields(prefix, typeValue));
    if (!meta.save) return out;
    const fd = new FormData();
    callTypeHandler(meta, 'save', prefix, fd);
    for (const [key, value] of fd.entries()) {
      out[key] = value;
    }
    return out;
  }

  function normalizeSnapshotLayout(snapshot, index, tab = currentTileTab) {
    const fallbackCol = (index >= 0) ? ((index % GRID_COLS) + 1) : 1;
    const firstRow = firstAllowedGridRow(tab);
    const fallbackRow = (index >= 0)
      ? (Math.max(firstRow, Math.floor(index / GRID_COLS)) + 1)
      : (firstRow + 1);
    let col = clampHalf(snapshot?.col, 1, GRID_COLS, fallbackCol);
    let row = clampHalf(snapshot?.row, firstRow + 1, GRID_ROWS + 0.5, fallbackRow);
    let spanW = clampHalf(snapshot?.span_w, 0.5, GRID_COLS, 1);
    let spanH = clampHalf(snapshot?.span_h, 0.5, GRID_ROWS, 1);
    return constrainLayoutToTab(
      normalizeLayoutForTileType(snapshot?.type, col - 1, row - 1,
                                 spanW, spanH),
      tab);
  }

  function buildTileSnapshotFromInputs(tab) {
    const prefix = tab;
    const colorEl = document.getElementById(prefix + '_tile_color');
    const snapshot = {
      type: document.getElementById(prefix + '_tile_type')?.value || '0',
      title: document.getElementById(prefix + '_tile_title')?.value || '',
      icon: document.getElementById(prefix + '_tile_icon')?.value || '',
      color: colorEl?.value || '#2A2A2A',
      bg_color_default: tileColorInputIsDefault(tab) ? '1' : '0',
      col: document.getElementById(prefix + '_tile_col')?.value || '1',
      row: document.getElementById(prefix + '_tile_row')?.value || '1',
      span_w: document.getElementById(prefix + '_tile_span_w')?.value || '1',
      span_h: document.getElementById(prefix + '_tile_span_h')?.value || '1'
    };
    Object.assign(snapshot, collectTypeFieldValues(tab));
    return snapshot;
  }

  function getTileSnapshotForSave(tab, index) {
    const draft = drafts[tab] && drafts[tab][index];
    if (draft && draft._dirty) return Object.assign({}, draft);
    if (currentTileTab === tab && currentTileIndex === index) return buildTileSnapshotFromInputs(tab);
    return null;
  }

  function applySnapshotToTileData(tab, index, snapshot) {
    const tiles = getTilesData(tab);
    if (!Array.isArray(tiles) || index < 0) return;

    const prev = tiles[index] || {};
    // A deleted (empty) tile starts from nothing: merged over the previous
    // data, its entity and options returned with the next tile in the slot.
    const tile = Number(snapshot?.type) === 0 ? {} : Object.assign({}, prev);
    const layout = normalizeSnapshotLayout(snapshot, index, tab);
    const numericFields = ['type', 'sensor_decimals', 'sensor_value_font', 'sensor_display_mode', 'sensor_gauge_min', 'sensor_gauge_max', 'switch_style', 'navigate_target', 'popup_open_mode', 'key_code', 'key_modifier', 'background_opacity', 'icon_disc', 'icon_glow', 'ss_opacity', 'ss_fill', 'ss_icon'];

    tile.type = clampInt(snapshot?.type, 0, 255, Number(prev.type) || 0);
    tile.title = snapshot?.title || '';
    tile.icon_name = snapshot?.icon || '';
    tile.bg_color = snapshotBgColorIsDefault(snapshot)
                        ? 0
                        : makeTileBgValue(hexToRgb(snapshot?.color || '#2A2A2A'));
    tile.col = layout.col;
    tile.row = layout.row;
    tile.span_w = layout.span_w;
    tile.span_h = layout.span_h;

    for (const [key, value] of Object.entries(snapshot || {})) {
      if (key === '_dirty' || key === '_rev' || key === 'icon' || key === 'color' || key === 'bg_color_default' || key === 'col' || key === 'row' || key === 'span_w' || key === 'span_h' || key === 'type' || key === 'title') continue;
      if (numericFields.includes(key)) {
        const num = Number(value);
        tile[key] = Number.isFinite(num) ? num : value;
      } else {
        tile[key] = value;
      }
    }

    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'switch_entity')) {
      tile.sensor_entity = snapshot.switch_entity || '';
    }
    for (const kind of ['number', 'select', 'datetime']) {
      if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, kind + '_entity')) tile.sensor_entity = snapshot[kind + '_entity'] || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'binary_sensor_entity')) {
      tile.sensor_entity = snapshot.binary_sensor_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'weather_entity')) {
      tile.sensor_entity = snapshot.weather_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'energy_entity')) {
      tile.sensor_entity = snapshot.energy_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'climate_entity')) {
      tile.sensor_entity = snapshot.climate_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'cover_entity')) {
      tile.sensor_entity = snapshot.cover_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'camera_entity')) {
      tile.sensor_entity = snapshot.camera_entity || '';
    }
    for (const kind of ['lock', 'alarm', 'fan']) {
      if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, kind + '_entity')) tile.sensor_entity = snapshot[kind + '_entity'] || '';
    }
    if (snapshot && (Object.prototype.hasOwnProperty.call(snapshot, 'clock_show_time') || Object.prototype.hasOwnProperty.call(snapshot, 'clock_show_date'))) {
      let flags = 0;
      if (String(snapshot.clock_show_time || '0') === '1') flags |= 1;
      if (String(snapshot.clock_show_date || '0') === '1') flags |= 2;
      if (flags === 0) flags = 1;
      tile.sensor_decimals = flags;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'clock_time_format')) {
      const num = Number(snapshot.clock_time_format);
      tile.sensor_gauge_min = Number.isFinite(num) ? num : 0;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'clock_date_format')) {
      const num = Number(snapshot.clock_date_format);
      tile.sensor_gauge_max = Number.isFinite(num) ? num : 0;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'animation_fit')) {
      const num = Number(snapshot.animation_fit);
      tile.sensor_display_mode = Number.isFinite(num) ? num : 0;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'animation_zoom')) {
      const num = Number(snapshot.animation_zoom);
      tile.sensor_gauge_max = Number.isFinite(num) ? num : 100;
    }

    if ([8,9,10].includes(Number(tile.type)) && snapshot?.tile_border !== undefined) {
      tile.sensor_display_mode = ['0','false'].includes(String(snapshot.tile_border)) ? 1 : 0;
    }
    if (Number(tile.type) === 12 && snapshot?.weather_colored_icons !== undefined) {
      tile.sensor_display_mode = ['0','false'].includes(String(snapshot.weather_colored_icons)) ? 1 : 0;
    }
    tiles[index] = tile;
    tilesData[tab] = tiles;
  }

  function markLatestSaveRequest(tab, index, requestId) {
    if (!latestSaveRequestByTab[tab]) latestSaveRequestByTab[tab] = {};
    latestSaveRequestByTab[tab][index] = requestId;
  }

  function isLatestSaveRequest(tab, index, requestId) {
    return !!(latestSaveRequestByTab[tab] && latestSaveRequestByTab[tab][index] === requestId);
  }

  function getTileSaveKey(tab, index) {
    return tab + ':' + index;
  }

  function queueSaveAfterFlight(tab, index, silent = true) {
    const saveKey = getTileSaveKey(tab, index);
    const existing = queuedSaveByTile[saveKey];
    queuedSaveByTile[saveKey] = {
      silent: existing ? (existing.silent && silent) : silent
    };
  }

  function flushQueuedSave(tab, index) {
    const saveKey = getTileSaveKey(tab, index);
    if (saveInFlightByTile[saveKey]) return;
    const queued = queuedSaveByTile[saveKey];
    if (!queued) return;
    delete queuedSaveByTile[saveKey];
    saveTile(tab, queued.silent, index);
  }
