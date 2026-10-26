#include "src/ui/timers/voice_timers.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "src/audio/speaker.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"

namespace voice_timers {
namespace {

constexpr const char* kLogPrefix = "[Timers]";
constexpr size_t kMaxFinished = 4;

// PSRAM: the list is small but never needs internal RAM.
struct State {
  voice_timer::TimerList list;
  uint32_t received_ms;
  voice_timer::Timer finished[kMaxFinished];
  uint8_t finished_count;
};
State* g_state = nullptr;
uint32_t g_generation = 0;
uint8_t g_sound = voice_timer::kDefaultSound;
uint32_t g_log_ms = 0;

State* state() {
  if (!g_state) {
    g_state = static_cast<State*>(heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_state) g_state = static_cast<State*>(calloc(1, sizeof(State)));
  }
  return g_state;
}

bool logDue() {
  const uint32_t now = millis();
  if (g_log_ms && now - g_log_ms < 10000) return false;
  g_log_ms = now;
  return true;
}

bool publish(const char* payload, size_t length) {
  if (!length) return false;
  const char* topic = mqttTopics.topic(TopicKey::ASSIST_TIMER_STAT);
  if (!topic || !*topic || !networkManager.isMqttConnected()) {
    Serial.printf("%s Request not sent: no Bridge connection\n", kLogPrefix);
    return false;
  }
  return networkManager.mqttEnqueuePublish(topic, payload, false);
}

void addFinished(State* s, const voice_timer::Timer& timer) {
  // A repeated message for the same timer rings once.
  for (uint8_t i = 0; i < s->finished_count; ++i) {
    if (strcmp(s->finished[i].id, timer.id) == 0) return;
  }
  if (s->finished_count == kMaxFinished) {
    memmove(&s->finished[0], &s->finished[1], sizeof(s->finished[0]) * (kMaxFinished - 1));
    --s->finished_count;
  }
  s->finished[s->finished_count++] = timer;
}

}  // namespace

bool supported() { return speaker::supported(); }

bool handleCommandPayload(const uint8_t* payload, size_t length) {
  if (!payload || length == 0 || length > voice_timer::kMaxPayloadBytes) return false;
  // The scanners need a NUL-terminated copy.
  char* text = static_cast<char*>(heap_caps_malloc(length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!text) return false;
  memcpy(text, payload, length);
  text[length] = '\0';
  const voice_timer::Op op = voice_timer::opOf(text, length);
  State* s = op == voice_timer::Op::None ? nullptr : state();
  if (op != voice_timer::Op::None && !s) {
    heap_caps_free(text);
    return true;
  }
  switch (op) {
    case voice_timer::Op::None:
      break;
    case voice_timer::Op::Timers: {
      voice_timer::TimerList list;
      if (voice_timer::parseTimers(text, length, &list)) {
        s->list = list;
        s->received_ms = millis();
        ++g_generation;
        if (logDue()) Serial.printf("%s %u timer(s)\n", kLogPrefix, static_cast<unsigned>(list.count));
      } else if (logDue()) {
        Serial.printf("%s Timer list ignored: no list\n", kLogPrefix);
      }
      break;
    }
    case voice_timer::Op::Done: {
      voice_timer::Timer timer;
      if (voice_timer::parseDone(text, length, &timer)) {
        addFinished(s, timer);
        // It is no longer running.
        for (uint8_t i = 0; i < s->list.count; ++i) {
          if (strcmp(s->list.timers[i].id, timer.id) != 0) continue;
          memmove(&s->list.timers[i], &s->list.timers[i + 1],
                  sizeof(s->list.timers[0]) * (s->list.count - i - 1));
          --s->list.count;
          ++g_generation;
          break;
        }
        Serial.printf("%s Timer %s finished\n", kLogPrefix, timer.id);
      } else if (logDue()) {
        Serial.printf("%s Finished timer ignored: invalid id\n", kLogPrefix);
      }
      break;
    }
    case voice_timer::Op::Sound: {
      const int sound = voice_timer::parseSound(text, length);
      if (sound >= 0) {
        if (g_sound != sound) Serial.printf("%s Sound %s\n", kLogPrefix, voice_timer::kSoundIds[sound]);
        g_sound = static_cast<uint8_t>(sound);
      } else if (logDue()) {
        Serial.printf("%s Unknown timer sound ignored\n", kLogPrefix);
      }
      break;
    }
  }
  heap_caps_free(text);
  return op != voice_timer::Op::None;
}

uint32_t generation() { return g_generation; }

uint8_t count() { return g_state ? g_state->list.count : 0; }

bool timerAt(uint8_t index, voice_timer::Timer* out, uint32_t* left_s) {
  if (!g_state || index >= g_state->list.count) return false;
  const voice_timer::Timer& timer = g_state->list.timers[index];
  if (out) *out = timer;
  if (left_s) *left_s = voice_timer::secondsLeft(timer, millis() - g_state->received_ms);
  return true;
}

bool nextTimer(voice_timer::Timer* out, uint32_t* left_s) {
  if (!g_state || g_state->list.count == 0) return false;
  int best = -1;
  uint32_t best_left = 0;
  bool best_active = false;
  for (uint8_t i = 0; i < g_state->list.count; ++i) {
    const voice_timer::Timer& timer = g_state->list.timers[i];
    const uint32_t left = voice_timer::secondsLeft(timer, millis() - g_state->received_ms);
    const bool better = best < 0 || (timer.active && !best_active) ||
                        (timer.active == best_active && left < best_left);
    if (!better) continue;
    best = i;
    best_left = left;
    best_active = timer.active;
  }
  if (out) *out = g_state->list.timers[best];
  if (left_s) *left_s = best_left;
  return true;
}

bool takeFinished(voice_timer::Timer* out) {
  if (!g_state || g_state->finished_count == 0) return false;
  if (out) *out = g_state->finished[0];
  memmove(&g_state->finished[0], &g_state->finished[1], sizeof(g_state->finished[0]) * (kMaxFinished - 1));
  --g_state->finished_count;
  return true;
}

bool hasFinished() { return g_state && g_state->finished_count > 0; }

uint8_t sound() { return g_sound; }

bool requestStart(uint32_t seconds, const char* name) {
  char payload[160];
  return publish(payload, voice_timer::buildStart(seconds, name, payload, sizeof(payload)));
}

bool requestAction(const char* op, const char* id) {
  char payload[96];
  return publish(payload, voice_timer::buildAction(op, id, payload, sizeof(payload)));
}

bool requestAdd(const char* id, uint32_t seconds) {
  char payload[96];
  return publish(payload, voice_timer::buildAdd(id, seconds, payload, sizeof(payload)));
}

}  // namespace voice_timers
