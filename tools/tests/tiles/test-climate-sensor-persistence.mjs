// Climate temperature/humidity sensors live in the /_tile_climate_sensors
// sidecar (PackedTileV7 has no room for two entity IDs). Compiles the real
// tile_config.cpp sidecar functions and tile_config.h helpers on the host:
// save, reboot, unchanged no-op, clear, .tmp/.bak recovery, foreign records,
// other tile types and failed writes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/tiles/config/tile_config.cpp'), 'utf8');
const header = fs.readFileSync(path.join(root, 'src/tiles/config/tile_config.h'), 'utf8');
const fnIn = (text, name) => {
  const f = cppFunctionDefinitions(text).find(candidate => candidate.name === name);
  assert(f, name);
  return f.source;
};
const fn = name => fnIn(source, name);
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: Climate sensor persistence needs a native C++ compiler'); process.exit(0); }

// Every save, load, delete and no-op path carries the sidecar.
assert(cppFunctionDefinitions(source).some(f => f.name === 'TileConfig::loadGrid' && f.source.includes('applyClimateSensorsFromSd(folder_id, grid);')));
assert(cppFunctionDefinitions(source).some(f => f.name === 'TileConfig::saveGridInPlace' &&
  f.source.includes('normalizeClimateSensors(working.tiles[i]);') &&
  f.source.includes('writeClimateSensorsSd(folder_id, grid_idx, climateSensorsRecord(tile))')));
assert(cppFunctionDefinitions(source).some(f => f.name === 'TileConfig::deleteFolder' && f.source.includes('writeClimateSensorsSd(id, i, "");')));
assert(fn('gridSidecarsMatchStored').includes('readClimateSensorsSd(folder_id, index, stored_sensors)'));
assert(fn('TileConfig::loadFolderGridEntitiesOnly').includes('out[i].extra_entities = climateSensorsRecord(sensors);'));

const cpp = String.raw`
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
class String:public std::string {public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}bool startsWith(const String&s)const{return rfind(s,0)==0;}};
std::map<String,String> files; std::set<String> directories; bool ready=true,fail_write=false;int writes=0;
constexpr int FILE_READ=0,FILE_WRITE=1;
struct File {String path;std::vector<String> entries;size_t cursor=0;bool valid=false,dir=false;
 explicit operator bool()const{return valid;} bool isDirectory()const{return dir;}
 const char* name()const{return path.c_str()+path.find_last_of('/')+1;}
 File openNextFile(){if(cursor>=entries.size())return {};return {entries[cursor++],{},0,true,false};}
 size_t size()const{return files[path].size();} String readString(){return files[path];}
 size_t print(const String&s){++writes;files[path]=fail_write?String(s.substr(0,3)):s;return files[path].size();}
 void close(){}void flush(){}
};
struct FS {bool exists(const String&p){return files.count(p)||directories.count(p);}bool mkdir(const String&p){directories.insert(p);return true;}
 bool remove(const String&p){return files.erase(p)>0;}
 bool rename(const String&a,const String&b){if(!files.count(a)||files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
 File open(const String&p,int mode=FILE_READ){
 if(directories.count(p)){File f;f.path=p;f.valid=true;f.dir=true;for(const auto&e:files)if(e.first.startsWith(p+"/"))f.entries.push_back(e.first);return f;}
 if(mode==FILE_WRITE)files[p]="";
 return {p,{},0,files.count(p)>0,false};
 }} filesystem;
FS& storageFS(){return filesystem;}bool storageReady(){return ready;}
constexpr size_t TILES_PER_GRID=4;enum TileType{TILE_EMPTY=0,TILE_SENSOR=1,TILE_CLIMATE=17};
const char*kTitlePathDir="/_tile_titles",*kImagePathDir="/_tile_images",*kEntityPathDir="/_tile_entities",*kIconColorPathDir="/_tile_icon_colors",*kClimateSensorPathDir="/_tile_climate_sensors";
bool g_sidecar_index_built=false;std::vector<uint32_t> g_title_sidecar_keys,g_image_sidecar_keys,g_entity_sidecar_keys,g_icon_color_sidecar_keys,g_climate_sensor_sidecar_keys;
using PsString=std::string;struct SidecarText{uint32_t key;PsString text;};using SidecarTexts=std::vector<SidecarText>;
SidecarTexts g_image_sidecar_texts,g_entity_sidecar_texts,g_title_sidecar_texts,g_icon_color_sidecar_texts,g_climate_sensor_sidecar_texts;
struct SidecarTextsGuard{SidecarTextsGuard(){}~SidecarTextsGuard(){}};
struct Tile {TileType type=TILE_EMPTY;String climate_temperature_sensor;String climate_humidity_sensor;};
struct TileGridConfig{Tile tiles[TILES_PER_GRID];};
static constexpr size_t kClimateSensorEntityMax = 128;
` + ['normalizeClimateSensorEntity', 'normalizeClimateSensors', 'climateSensorsRecord', 'applyClimateSensorsRecord']
  .map(name => fnIn(header, name)).join('\n') + '\nstatic constexpr size_t kClimateSensorRecordMax = 2 * kClimateSensorEntityMax;\n' +
['sidecarKey', 'sidecarKeyPresent', 'sidecarKeyAdd', 'sidecarTextCached', 'sidecarTextStore', 'sidecarTextForget', 'sidecarTextsFor',
 'sidecarKeyRemove', 'scanSidecarDir', 'ensureSidecarIndexBuilt', 'tmpPathFor', 'backupPathFor', 'replaceFileWithPreparedTmp',
 'climateSensorPathFile', 'readClimateSensorsSd', 'writeClimateSensorsSd', 'applyClimateSensorsFromSd'].map(fn).join('\n') + String.raw`
void reboot(){g_sidecar_index_built=false;g_climate_sensor_sidecar_keys.clear();g_climate_sensor_sidecar_texts.clear();}
Tile climate(const char* t,const char* h){Tile tile;tile.type=TILE_CLIMATE;tile.climate_temperature_sensor=t;tile.climate_humidity_sensor=h;return tile;}
int main(){
 // Normalization: trimmed ids with a domain; junk and other types are dropped.
 Tile t=climate("  sensor.living_room_temperature ","sensor.living_room_humidity\r\n");normalizeClimateSensors(t);
 assert(t.climate_temperature_sensor=="sensor.living_room_temperature"&&t.climate_humidity_sensor=="sensor.living_room_humidity");
 t=climate("no_domain",".sensor");normalizeClimateSensors(t);assert(t.climate_temperature_sensor.empty()&&t.climate_humidity_sensor.empty());
 t=climate("sensor.a b","sensor.x|y");normalizeClimateSensors(t);assert(t.climate_temperature_sensor.empty()&&t.climate_humidity_sensor.empty());
 t=climate(("sensor."+std::string(130,'x')).c_str(),"");normalizeClimateSensors(t);assert(t.climate_temperature_sensor.empty());
 t=climate("sensor.t","sensor.h");t.type=TILE_SENSOR;normalizeClimateSensors(t);assert(t.climate_temperature_sensor.empty()&&climateSensorsRecord(t).empty());
 assert(climateSensorsRecord(climate("","")).empty());
 assert(climateSensorsRecord(climate("sensor.t",""))=="sensor.t\n");
 assert(climateSensorsRecord(climate("","sensor.h"))=="\nsensor.h");

 // Save, reboot and load; only the humidity sensor; unchanged saves write nothing.
 Tile both=climate("sensor.t","sensor.h");
 assert(writeClimateSensorsSd(3,1,climateSensorsRecord(both)));
 TileGridConfig grid;grid.tiles[1].type=TILE_CLIMATE;reboot();applyClimateSensorsFromSd(3,grid);
 assert(grid.tiles[1].climate_temperature_sensor=="sensor.t"&&grid.tiles[1].climate_humidity_sensor=="sensor.h");
 int before=writes;assert(writeClimateSensorsSd(3,1,climateSensorsRecord(both)));assert(writes==before);
 assert(writeClimateSensorsSd(3,1,climateSensorsRecord(climate("","sensor.h2"))));
 grid=TileGridConfig{};grid.tiles[1].type=TILE_CLIMATE;reboot();applyClimateSensorsFromSd(3,grid);
 assert(grid.tiles[1].climate_temperature_sensor.empty()&&grid.tiles[1].climate_humidity_sensor=="sensor.h2");

 // A non-Climate tile in the slot ignores a stale file.
 grid=TileGridConfig{};grid.tiles[1].type=TILE_SENSOR;applyClimateSensorsFromSd(3,grid);
 assert(grid.tiles[1].climate_humidity_sensor.empty());

 // Clearing removes the file; nothing comes back after a reboot.
 assert(writeClimateSensorsSd(3,1,""));reboot();String record;assert(!readClimateSensorsSd(3,1,record));
 assert(!files.count(climateSensorPathFile(3,1)));

 // Recovery from .tmp and .bak; a foreign record is normalized on load.
 auto path=climateSensorPathFile(5,0);
 files[tmpPathFor(path)]="sensor.t\nsensor.h";directories.insert(kClimateSensorPathDir);reboot();
 assert(readClimateSensorsSd(5,0,record)&&record=="sensor.t\nsensor.h");
 files.erase(tmpPathFor(path));files[backupPathFor(path)]="sensor.t\nsensor.h";reboot();
 assert(readClimateSensorsSd(5,0,record)&&record=="sensor.t\nsensor.h");
 files.erase(backupPathFor(path));files[path]="bad id\nsensor.ok\nextra";reboot();
 grid=TileGridConfig{};grid.tiles[0].type=TILE_CLIMATE;applyClimateSensorsFromSd(5,grid);
 assert(grid.tiles[0].climate_temperature_sensor.empty()&&grid.tiles[0].climate_humidity_sensor.empty());

 // A failed write keeps the previous record; oversized records are refused.
 assert(writeClimateSensorsSd(6,2,"sensor.t\nsensor.h"));fail_write=true;
 assert(!writeClimateSensorsSd(6,2,"sensor.t2\nsensor.h"));fail_write=false;
 assert(readClimateSensorsSd(6,2,record)&&record=="sensor.t\nsensor.h");
 assert(!writeClimateSensorsSd(6,2,String(300,'x')));ready=false;assert(!writeClimateSensorsSd(6,2,"sensor.a\n"));
 std::cout<<"Climate sensor sidecar: normalize, save/reboot, no-op, clear, other types, recovery and failed writes passed\n";
}
`;
const out = path.join(root, 'build/tests/climate-sensor-persistence');
fs.mkdirSync(out, {recursive: true});
const cppPath = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(cppPath, cpp);
let r = spawnSync(compiler, ['-std=c++17', cppPath, '-o', binary], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
r = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
console.log(r.stdout.trim());
