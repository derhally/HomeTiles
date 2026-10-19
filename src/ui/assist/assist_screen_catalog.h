#pragma once

// ArduinoJson parser for {base}/assist/screens (assist_contract.h): the
// screen files the Bridge offers. Kept apart from the store so the host test
// can run it without the firmware.

#include <ArduinoJson.h>
#include <stddef.h>
#include <string.h>

#include "src/audio/assist_contract.h"

namespace assist_screen_catalog {

struct Entry {
  char id[assist_contract::kMaxWakeWordIdLength + 1];
  char name[33];  // shown in Settings; the id when the file names none
};

// Fills out with up to max valid entries, in the Bridge's order: ids follow
// the wake word id rule, never repeat a built-in style or an earlier entry,
// and names lose control characters. Returns the count, or -1 when the
// payload is not a version 1 catalog. An empty payload is an empty catalog.
inline int parse(const char* payload, size_t length, Entry* out, size_t max) {
  if (!payload || length == 0) return 0;
  if (length > 4096) return -1;
  JsonDocument doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) return -1;
  JsonVariantConst version = doc["v"];
  if (!version.is<long long>() || version.as<long long>() != assist_contract::kProtocolVersion) return -1;
  JsonVariantConst screens = doc["screens"];
  if (!screens.is<JsonArrayConst>()) return -1;
  size_t count = 0;
  for (JsonVariantConst item : screens.as<JsonArrayConst>()) {
    if (count >= max) break;
    const char* id = item["id"].is<const char*>() ? item["id"].as<const char*>() : nullptr;
    if (!id || !assist_contract::isValidWakeWordId(id, strlen(id)) || assist_contract::screenStyleIndex(id) >= 0) continue;
    bool repeated = false;
    for (size_t i = 0; i < count; ++i) repeated = repeated || strcmp(out[i].id, id) == 0;
    if (repeated) continue;
    Entry& e = out[count];
    memcpy(e.id, id, strlen(id) + 1);
    const char* name = item["name"].is<const char*>() ? item["name"].as<const char*>() : nullptr;
    if (!name || !name[0]) name = id;
    size_t n = 0;
    for (const char* c = name; *c && n + 1 < sizeof(e.name); ++c) {
      const unsigned char ch = static_cast<unsigned char>(*c);
      e.name[n++] = ch < 0x20 ? ' ' : static_cast<char>(ch);
    }
    // Do not end inside a UTF-8 sequence cut by the size limit.
    size_t lead = n;
    while (lead > 0 && (static_cast<unsigned char>(e.name[lead - 1]) & 0xC0) == 0x80) --lead;
    if (lead > 0) {
      const unsigned char first = static_cast<unsigned char>(e.name[lead - 1]);
      const size_t want = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC0 ? 2 : 1;
      if (n - (lead - 1) < want) n = lead - 1;
    }
    e.name[n] = '\0';
    ++count;
  }
  return static_cast<int>(count);
}

}  // namespace assist_screen_catalog
