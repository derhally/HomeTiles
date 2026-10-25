// Graph-mode Sensor tiles show 24 hours. History replies for the Sensor
// popup's other ranges (30m, 1H, 12H, 7D) reach the tile handler as well and
// must not replace the tile's 24-hour graph.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const renderer = readRepoFile('src/tiles/runtime/tile_renderer.cpp');
const apply = cppFunctionDefinitions(renderer).find(f => f.name === 'apply_tile_graph_history');
assert(apply, 'apply_tile_graph_history');
const body = apply.source;
const guard = body.search(/extract_json_number_field\(json, "hours", hours\) && lroundf\(hours\) != 24\) return;/);
assert(guard > 0, 'A reply for another range than 24 hours leaves the tile graph alone');
assert(guard < body.indexOf('for (uint8_t i = 0; i < TILES_PER_GRID; ++i)'),
  'The range is checked before any tile is touched');
const request = cppFunctionDefinitions(renderer).find(f => f.name === 'request_tile_graph_history');
assert.match(request.source, /mqttPublishHistoryRequest\(entity_id\);/, 'Tiles request the default 24-hour history');
console.log('Tile graph history: only 24-hour replies update graph tiles');
