#include "src/ui/assist/assist_timing.h"

#if HOMETILES_ASSIST_TIMING_ACTIVE

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

#include <esp_heap_caps.h>

namespace assist_timing {
namespace {

constexpr int kMaxScreens = 16;
constexpr int kBuckets = 160;  // draw time in 1 ms steps; the last holds the rest
constexpr uint32_t kFramePeriodUs = 33000;
constexpr size_t kIdSize = 33;

struct Stats {
  uint32_t frames;    // refreshes that drew
  uint64_t draw_us;   // sum, refresh start to ready
  uint32_t draw_max_us;
  uint64_t copy_us;   // sum of the flush callback's time within those refreshes
  uint64_t px;        // pixels flushed
  uint32_t late;      // refreshes longer than the frame period
  uint32_t ticks;     // overlay timer ticks
  uint64_t work_us;   // the overlay's own work per tick
  uint32_t work_max_us;
  uint32_t gaps;      // intervals between ticks
  uint64_t gap_us;
  uint32_t gap_max_us;
  uint16_t hist[kBuckets];
};

struct Slot {
  char id[kIdSize];
  bool from_file;
  bool used;
  uint32_t open_us;  // the first refresh after begin(): the whole screen
  Stats phases[kPhaseCount];
};

Slot* g_slots = nullptr;
int g_slot = -1;
Phase g_phase = kListening;
bool g_active = false;
bool g_attached = false;
bool g_first_refresh = false;
// The refresh being measured.
bool g_in_refresh = false;
uint32_t g_refresh_started = 0;
uint32_t g_flush_started = 0;
uint32_t g_flush_us = 0;
uint32_t g_flushes = 0;
uint32_t g_px = 0;
uint32_t g_last_tick_us = 0;
bool g_have_tick = false;

bool ensureSlots() {
  if (g_slots) return true;
  const size_t bytes = sizeof(Slot) * kMaxScreens;
  void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  if (!memory) return false;
  g_slots = static_cast<Slot*>(memory);
  memset(static_cast<void*>(g_slots), 0, bytes);
  return true;
}

Stats* current() {
  if (!g_active || g_slot < 0 || !g_slots) return nullptr;
  return &g_slots[g_slot].phases[g_phase];
}

void recordDraw(uint32_t draw_us, uint32_t copy_us, uint32_t px) {
  if (g_first_refresh) {
    // The opening draws the whole screen; it is not a frame of the animation.
    g_first_refresh = false;
    g_slots[g_slot].open_us = draw_us;
    return;
  }
  Stats* s = current();
  if (!s) return;
  ++s->frames;
  s->draw_us += draw_us;
  if (draw_us > s->draw_max_us) s->draw_max_us = draw_us;
  s->copy_us += copy_us;
  s->px += px;
  if (draw_us > kFramePeriodUs) ++s->late;
  uint32_t bucket = draw_us / 1000;
  if (bucket >= static_cast<uint32_t>(kBuckets)) bucket = kBuckets - 1;
  if (s->hist[bucket] < 0xFFFF) ++s->hist[bucket];
}

void onDisplayEvent(lv_event_t* e) {
  if (!g_active) return;
  const lv_event_code_t code = lv_event_get_code(e);
  switch (code) {
    case LV_EVENT_REFR_START:
      g_in_refresh = true;
      g_refresh_started = micros();
      g_flush_us = 0;
      g_flushes = 0;
      g_px = 0;
      break;
    case LV_EVENT_FLUSH_START: {
      if (!g_in_refresh) break;
      g_flush_started = micros();
      ++g_flushes;
      const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(e));
      if (area) g_px += static_cast<uint32_t>(lv_area_get_size(area));
      break;
    }
    case LV_EVENT_FLUSH_FINISH:
      if (g_in_refresh) g_flush_us += micros() - g_flush_started;
      break;
    case LV_EVENT_REFR_READY: {
      if (!g_in_refresh) break;
      g_in_refresh = false;
      // A refresh without a flush drew nothing (the timer found no change).
      if (g_flushes) recordDraw(micros() - g_refresh_started, g_flush_us, g_px);
      break;
    }
    default: break;
  }
}

// The 95th percentile from the histogram, in microseconds (bucket upper edge).
uint32_t p95(const Stats& s) {
  if (!s.frames) return 0;
  const uint32_t target = (s.frames * 95 + 99) / 100;
  uint32_t seen = 0;
  for (int b = 0; b < kBuckets; ++b) {
    seen += s.hist[b];
    if (seen >= target) return static_cast<uint32_t>(b + 1) * 1000;
  }
  return static_cast<uint32_t>(kBuckets) * 1000;
}

void appendU32(String& out, const char* key, uint32_t value) {
  out += '"';
  out += key;
  out += "\":";
  out += String(value);
}

uint32_t average(uint64_t sum, uint32_t count) { return count ? static_cast<uint32_t>(sum / count) : 0; }

}  // namespace

void attach() {
  if (g_attached) return;
  lv_display_t* display = lv_display_get_default();
  if (!display) return;
  lv_display_add_event_cb(display, onDisplayEvent, LV_EVENT_ALL, nullptr);
  g_attached = true;
}

void begin(const char* id, bool from_file) {
  g_active = false;
  g_slot = -1;
  if (!id || !ensureSlots()) return;
  int free_slot = -1;
  for (int i = 0; i < kMaxScreens; ++i) {
    Slot& slot = g_slots[i];
    if (slot.used && slot.from_file == from_file && strncmp(slot.id, id, kIdSize - 1) == 0) {
      g_slot = i;
      break;
    }
    if (!slot.used && free_slot < 0) free_slot = i;
  }
  if (g_slot < 0) {
    if (free_slot < 0) return;  // more screens than slots: the newest is not measured
    g_slot = free_slot;
    Slot& slot = g_slots[g_slot];
    memset(static_cast<void*>(&slot), 0, sizeof(slot));
    strncpy(slot.id, id, kIdSize - 1);
    slot.from_file = from_file;
    slot.used = true;
  }
  g_phase = kListening;
  g_active = true;
  g_first_refresh = true;
  g_in_refresh = false;
  g_have_tick = false;
}

void setPhase(Phase phase) {
  if (phase >= kPhaseCount) return;
  g_phase = phase;
  // The interval across a phase change belongs to neither phase.
  g_have_tick = false;
}

void end() {
  g_active = false;
  g_in_refresh = false;
  g_have_tick = false;
}

void noteFrame(uint32_t work_us) {
  Stats* s = current();
  if (!s) return;
  const uint32_t now = micros();
  ++s->ticks;
  s->work_us += work_us;
  if (work_us > s->work_max_us) s->work_max_us = work_us;
  if (g_have_tick) {
    const uint32_t gap = now - g_last_tick_us;
    ++s->gaps;
    s->gap_us += gap;
    if (gap > s->gap_max_us) s->gap_max_us = gap;
  }
  g_last_tick_us = now;
  g_have_tick = true;
}

void reset() {
  if (!g_slots) return;
  const int keep = g_slot;
  char id[kIdSize];
  bool from_file = false;
  if (g_active && keep >= 0) {
    memcpy(id, g_slots[keep].id, kIdSize);
    from_file = g_slots[keep].from_file;
  }
  memset(static_cast<void*>(g_slots), 0, sizeof(Slot) * kMaxScreens);
  if (g_active && keep >= 0) {
    // The screen on show keeps its slot, so its next frames still count.
    Slot& slot = g_slots[keep];
    memcpy(slot.id, id, kIdSize);
    slot.from_file = from_file;
    slot.used = true;
  }
  g_have_tick = false;
}

void toJson(String& out) {
  static const char* const kPhaseNames[kPhaseCount] = {"listening", "thinking", "answering"};
  out.reserve(out.length() + 2048);
  out += "{\"screens\":[";
  bool first_slot = true;
  for (int i = 0; g_slots && i < kMaxScreens; ++i) {
    const Slot& slot = g_slots[i];
    if (!slot.used) continue;
    if (!first_slot) out += ',';
    first_slot = false;
    out += "{\"id\":\"";
    // Ids are [a-z0-9_] by contract; anything else is dropped rather than escaped.
    for (const char* c = slot.id; *c; ++c) {
      if ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_') out += *c;
    }
    out += "\",\"file\":";
    out += slot.from_file ? "true" : "false";
    out += ',';
    appendU32(out, "open_us", slot.open_us);
    out += ",\"phases\":[";
    for (int p = 0; p < kPhaseCount; ++p) {
      const Stats& s = slot.phases[p];
      if (p) out += ',';
      out += "{\"phase\":\"";
      out += kPhaseNames[p];
      out += "\",";
      appendU32(out, "frames", s.frames);
      out += ',';
      appendU32(out, "draw_us", average(s.draw_us, s.frames));
      out += ',';
      appendU32(out, "p95_us", p95(s));
      out += ',';
      appendU32(out, "max_us", s.draw_max_us);
      out += ',';
      appendU32(out, "copy_us", average(s.copy_us, s.frames));
      out += ',';
      appendU32(out, "px", s.frames ? static_cast<uint32_t>(s.px / s.frames) : 0u);
      out += ',';
      appendU32(out, "late", s.late);
      out += ',';
      appendU32(out, "ticks", s.ticks);
      out += ',';
      appendU32(out, "work_us", average(s.work_us, s.ticks));
      out += ',';
      appendU32(out, "work_max_us", s.work_max_us);
      out += ',';
      appendU32(out, "gap_us", average(s.gap_us, s.gaps));
      out += ',';
      appendU32(out, "gap_max_us", s.gap_max_us);
      out += '}';
    }
    out += "]}";
  }
  out += "]}";
}

}  // namespace assist_timing

#endif  // HOMETILES_ASSIST_TIMING_ACTIVE
