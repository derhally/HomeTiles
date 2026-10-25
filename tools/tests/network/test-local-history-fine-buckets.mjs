// The panel's own temperature history stores one sample every 5 minutes. The
// 1-hour and 30-minute Sensor graphs ask for 1-minute buckets; each bucket must
// repeat the sample that covers it, so 60 buckets span one hour. Before, every
// bucket took the next sample and the "1 hour" graph showed 5 hours.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/network/mqtt/mqtt_handlers.cpp'), 'utf8').replace(/\r\n/g, '\n');
const builder = cppFunctionDefinitions(source).find(f => f.name === 'build_external_temp_history_payload');
assert(builder, 'build_external_temp_history_payload');
const constants = source.slice(source.indexOf('static constexpr uint16_t kExternalTempHistoryHoursMax'),
  source.indexOf('static uint8_t* g_external_history_storage'));
const cxx = ['clang++', 'g++'].find(c => spawnSync(c, ['--version']).status === 0);
if (!cxx) {
  console.log('SKIP: Local history buckets need a host C++ compiler');
  process.exit(0);
}

const cpp = `
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
class String : public std::string { public:
  using std::string::string; String() = default; String(const std::string& v) : std::string(v) {}
  explicit String(unsigned v) : std::string(std::to_string(v)) {}
  explicit String(int v) : std::string(std::to_string(v)) {}
  void reserve(size_t n) { std::string::reserve(n); }
  String& operator+=(const char* v) { append(v); return *this; }
  String& operator+=(const String& v) { append(v); return *this; }
  size_t length() const { return size(); }
};
char* dtostrf(double v, int, unsigned decimals, char* out) { sprintf(out, "%.*f", decimals, v); return out; }
constexpr const char* kEntityExternalTemperature = "sensor.panel_temperature";
${constants}
static float g_external_history_values[kExternalTempHistoryPoints];
static bool g_external_history_valid[kExternalTempHistoryPoints];
static uint16_t g_external_history_head = 0, g_external_history_count = 0;
static bool g_external_temp_valid = true;
static float g_external_temp_c = 21.0f;
${builder.source}
std::vector<std::string> values(const String& payload) {
  std::vector<std::string> out; size_t a = payload.find("[") + 1, b = payload.find("]");
  for (size_t p = a; p < b;) { size_t q = payload.find(",", p); if (q == std::string::npos || q > b) q = b; out.push_back(payload.substr(p, q - p)); p = q + 1; }
  return out;
}
int main() {
  // 24 samples, 5 minutes apart: the newest is 23.0, then 22.0, ... back to 0.0.
  for (int i = 0; i < 24; ++i) { g_external_history_values[i] = static_cast<float>(i); g_external_history_valid[i] = true; }
  g_external_history_head = 24; g_external_history_count = 24;
  const auto hour = values(build_external_temp_history_payload(nullptr, 1, 1, 60));
  assert(hour.size() == 60);
  // One hour is the newest 12 samples (12..23), each repeated for 5 buckets.
  for (int b = 0; b < 60; ++b) {
    char expected[16]; sprintf(expected, "%.1f", 12.0 + b / 5);
    assert(hour[b] == expected && "1-minute buckets repeat the 5-minute sample that covers them");
  }
  // Buckets of whole samples keep averaging as before.
  const auto day = values(build_external_temp_history_payload(nullptr, 1, 10, 6));
  assert(day.size() == 6 && day[0] == "12.5" && day[5] == "22.5");
  const auto same = values(build_external_temp_history_payload(nullptr, 1, 5, 12));
  assert(same.size() == 12 && same[0] == "12.0" && same[11] == "23.0");
  return 0;
}
`;
const out = path.join(root, 'build/tests/local-history-fine-buckets');
fs.mkdirSync(out, {recursive: true});
const file = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(file, cpp);
let result = spawnSync(cxx, ['-std=c++17', file, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log('Local temperature history: 1-minute buckets span one hour; whole-sample buckets unchanged');
