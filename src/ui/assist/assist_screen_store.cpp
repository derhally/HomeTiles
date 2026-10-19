#include "src/ui/assist/assist_screen_store.h"

#if HOMETILES_ASSIST_DATA_SCREENS

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "src/audio/assist_contract.h"
#include "src/audio/speaker.h"
#include "src/devices/device.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"
#include "src/ui/assist/assist_screen_catalog.h"

namespace assist_screen_store {
namespace {

namespace contract = assist_contract;
using assist_screen_catalog::Entry;

constexpr const char* kPrefsNamespace = "assist_scr";
constexpr const char* kPrefsChosenKey = "file";
constexpr const char* kPrefsHashKey = "hash";
constexpr const char* kDir = "/assist_ha";  // internal storage

Entry* g_entries = nullptr;  // kMaxFileScreens, PSRAM
const char* g_ids[contract::kMaxFileScreens];
size_t g_count = 0;
char g_chosen[contract::kMaxWakeWordIdLength + 1] = {};
uint32_t g_hash = 0;  // FNV-1a of the kept file, to skip unchanged rewrites
char g_error[contract::kMaxScreenErrorChars + 1] = {};
char g_subscribed[128] = {};  // the chosen file topic we follow

uint32_t fnv1a(const uint8_t* data, size_t length) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < length; ++i) h = (h ^ data[i]) * 16777619u;
  return h;
}

void keptPath(const char* id, char* out, size_t size) { snprintf(out, size, "%s/%s.json", kDir, id); }

void fileTopic(const char* id, char* out, size_t size) {
  snprintf(out, size, "%s%s%s", mqttTopics.deviceBase().c_str(), contract::kScreenLeaf, id);
}

// Follows the chosen file's topic (and leaves the previous one).
void followChosen() {
  char topic[sizeof(g_subscribed)] = {};
  if (g_chosen[0]) fileTopic(g_chosen, topic, sizeof(topic));
  if (strcmp(topic, g_subscribed) == 0) return;
  if (g_subscribed[0] && networkManager.isMqttConnected()) networkManager.mqttEnqueueUnsubscribe(g_subscribed);
  memcpy(g_subscribed, topic, sizeof(g_subscribed));
  if (g_subscribed[0] && networkManager.isMqttConnected()) networkManager.mqttEnqueueSubscribe(g_subscribed);
}

void storeChoice() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return;
  prefs.putString(kPrefsChosenKey, g_chosen);
  prefs.putUInt(kPrefsHashKey, g_hash);
  prefs.end();
}

void applyCatalog(const uint8_t* payload, size_t length) {
  if (!g_entries) return;
  const int n = assist_screen_catalog::parse(reinterpret_cast<const char*>(payload), length, g_entries,
                                             contract::kMaxFileScreens);
  if (n < 0) {
    Serial.printf("[Assist] Screen catalog from the Bridge ignored (%u bytes, not version %d)\n",
                  static_cast<unsigned>(length), contract::kProtocolVersion);
    return;
  }
  g_count = static_cast<size_t>(n);
  for (size_t i = 0; i < g_count; ++i) g_ids[i] = g_entries[i].id;
  Serial.printf("[Assist] Screen files offered by the Bridge: %u\n", static_cast<unsigned>(g_count));
  speaker::assistStatusChanged();
}

void keepFile(const char* id, const uint8_t* payload, size_t length) {
  if (strcmp(id, g_chosen) != 0) return;  // only the chosen file is kept
  if (length == 0) return;                // the Bridge withdrew it: keep the copy
  if (length > assist_screen::kMaxFileBytes) {
    Serial.printf("[Assist] Screen file %s from the Bridge skipped: %u bytes (limit %u)\n", id,
                  static_cast<unsigned>(length), static_cast<unsigned>(assist_screen::kMaxFileBytes));
    return;
  }
  const uint32_t hash = fnv1a(payload, length);
  char path[64];
  keptPath(id, path, sizeof(path));
  if (hash == g_hash && Device::storageFS().exists(path)) return;
  {
    Device::ScopedStorageWrite storage_write;
    fs::FS& store = Device::storageFS();
    if (!store.exists(kDir)) store.mkdir(kDir);
    File file = store.open(path, FILE_WRITE);
    const size_t written = file ? file.write(payload, length) : 0;
    if (file) file.close();
    if (written != length) {
      Serial.printf("[Assist] Screen file %s could not be kept (%u of %u bytes written)\n", id,
                    static_cast<unsigned>(written), static_cast<unsigned>(length));
      store.remove(path);
      return;
    }
  }
  g_hash = hash;
  g_error[0] = '\0';  // a new file gets a new chance
  storeChoice();
  Serial.printf("[Assist] Screen file %s from the Bridge kept (%u bytes)\n", id, static_cast<unsigned>(length));
  speaker::assistStatusChanged();
}

}  // namespace

void begin() {
  if (!g_entries) {
    g_entries = static_cast<Entry*>(
        heap_caps_calloc(contract::kMaxFileScreens, sizeof(Entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    const String chosen = prefs.getString(kPrefsChosenKey, "");
    if (contract::isValidWakeWordId(chosen.c_str(), chosen.length())) {
      memcpy(g_chosen, chosen.c_str(), chosen.length() + 1);
    }
    g_hash = prefs.getUInt(kPrefsHashKey, 0);
    prefs.end();
  }
  if (g_chosen[0]) Serial.printf("[Assist] Screen file chosen: %s\n", g_chosen);
}

void subscribeTopics() {
  if (!networkManager.isMqttConnected()) return;
  const String catalog = mqttTopics.deviceBase() + contract::kScreensLeaf;
  networkManager.mqttEnqueueSubscribe(catalog.c_str());
  g_subscribed[0] = '\0';  // a new session has no subscriptions
  followChosen();
}

bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length) {
  const String& base = mqttTopics.deviceBase();
  if (!topic || base.length() == 0 || strncmp(topic, base.c_str(), base.length()) != 0) return false;
  const char* leaf = topic + base.length();
  if (strcmp(leaf, contract::kScreensLeaf) == 0) {
    applyCatalog(payload, length);
    return true;
  }
  const size_t prefix = strlen(contract::kScreenLeaf);
  if (strncmp(leaf, contract::kScreenLeaf, prefix) == 0) {
    const char* id = leaf + prefix;
    if (contract::isValidWakeWordId(id, strlen(id))) keepFile(id, payload, length);
    return true;
  }
  return false;
}

size_t count() { return g_count; }
const char* id(size_t index) { return index < g_count ? g_entries[index].id : ""; }
const char* name(size_t index) { return index < g_count ? g_entries[index].name : ""; }

int indexOf(const char* id) {
  for (size_t i = 0; id && i < g_count; ++i) {
    if (strcmp(g_entries[i].id, id) == 0) return static_cast<int>(i);
  }
  return -1;
}

const char* const* ids() { return g_ids; }

const char* chosen() { return g_chosen[0] ? g_chosen : nullptr; }

bool choose(const char* id) {
  if (id && indexOf(id) < 0) return false;
  const char* next = id ? id : "";
  if (strcmp(next, g_chosen) == 0) return true;
  memcpy(g_chosen, next, strlen(next) + 1);
  g_hash = 0;
  g_error[0] = '\0';
  storeChoice();
  followChosen();
  Serial.printf("[Assist] Screen file chosen: %s\n", g_chosen[0] ? g_chosen : "(built-in styles)");
  speaker::assistStatusChanged();
  return true;
}

bool readChosen(char* buffer, size_t capacity, size_t* length) {
  if (!g_chosen[0] || !buffer || !length || !Device::storageReady()) return false;
  char path[64];
  keptPath(g_chosen, path, sizeof(path));
  fs::FS& store = Device::storageFS();
  if (!store.exists(path)) return false;
  File file = store.open(path, FILE_READ);
  const size_t size = file ? file.size() : 0;
  bool ok = size > 0 && size <= capacity && file.read(reinterpret_cast<uint8_t*>(buffer), size) == size;
  if (file) file.close();
  if (ok) *length = size;
  return ok;
}

void setError(const char* reason) {
  const char* next = reason ? reason : "";
  if (strncmp(next, g_error, sizeof(g_error) - 1) == 0) return;
  snprintf(g_error, sizeof(g_error), "%s", next);
  speaker::assistStatusChanged();
}

const char* error() { return g_error; }

}  // namespace assist_screen_store

#endif  // HOMETILES_ASSIST_DATA_SCREENS
