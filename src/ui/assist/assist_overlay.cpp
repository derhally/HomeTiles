#include "src/ui/assist/assist_overlay.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>

#include "src/audio/speaker.h"
#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/core/power/power_manager.h"
#include "src/fonts/ui_fonts.h"

namespace assist_overlay {
namespace {

using speaker::AssistUiPhase;

constexpr uint32_t kFramePeriodMs = 33;
constexpr uint32_t kActivityRefreshMs = 1000;
constexpr int kLobeCount = 3;
constexpr int kGlowCount = 3;
constexpr float kTwoPi = 6.2831853f;

// Deep navy backdrop; cyan, violet and magenta lobes over a white-cyan core.
constexpr uint32_t kBackdrop = 0x05070F;
constexpr uint32_t kLobeColors[kLobeCount] = {0x22D3EE, 0x7C3AED, 0xDB2777};
constexpr uint32_t kLobeShade[kLobeCount] = {0x0E7490, 0x4C1D95, 0x831843};
constexpr lv_opa_t kLobeOpa = 150;
constexpr uint32_t kGlowListening = 0x0EA5E9;
constexpr uint32_t kGlowThinking = 0x8B5CF6;
constexpr lv_opa_t kGlowOpa[kGlowCount] = {30, 18, 10};
constexpr float kGlowScale[kGlowCount] = {1.30f, 1.62f, 2.0f};
// Lobe rotation speeds (rad/s) while listening; unequal, so the outline
// never repeats exactly.
constexpr float kLobeSpeed[kLobeCount] = {0.9f, -1.3f, 1.7f};
// Orbit and spinner speeds. The 7B's single DPI frame buffer is scanned out
// while LVGL flushes partial bands, so a fast mover shows a tear line where a
// band ends; about 5 px per frame keeps that invisible.
constexpr float kOrbitSpeed = 0.9f;     // rad/s
constexpr float kSpinnerSpeed = 100.0f; // degrees/s
// Orbit mode: small satellites on three tilted elliptical orbits around the
// core, like an atom seen at an angle. Nearer satellites (lower half of each
// ellipse) draw larger and brighter, so the orbits read as 3D.
constexpr int kOrbitPlanes = 3;
constexpr int kSatsPerPlane = 3;
constexpr int kSatCount = kOrbitPlanes * kSatsPerPlane;
constexpr float kOrbitFlatten = 0.34f;  // ellipse height / width
constexpr float kPlaneTilt[kOrbitPlanes] = {0.0f, 1.0472f, 2.0944f};  // 0, 60, 120 deg
constexpr uint32_t kSatColors[kOrbitPlanes] = {0x67E8F9, 0xC4B5FD, 0xF9A8D4};

lv_obj_t* g_overlay = nullptr;
lv_obj_t* g_glow[kGlowCount] = {};
lv_obj_t* g_lobes[kLobeCount] = {};
lv_obj_t* g_core = nullptr;
lv_obj_t* g_arc = nullptr;
lv_obj_t* g_arc_inner = nullptr;
lv_obj_t* g_sats[kSatCount] = {};
lv_obj_t* g_label = nullptr;
lv_timer_t* g_timer = nullptr;

AssistUiPhase g_shown = AssistUiPhase::Idle;
uint32_t g_started_tick = 0;
uint32_t g_activity_ms = 0;
// The display slept when the conversation started; it sleeps again after.
bool g_woke_display = false;

// Animation state, eased toward its targets every frame.
float g_level = 0.0f;   // voice or answer loudness, 0..1
float g_mode = 0.0f;    // 0 = listening sphere, 1 = orbit
float g_appear = 0.0f;  // pop-in scale, 0..1
float g_floor_db = -60.0f;

const i18n::Strings& tr() { return i18n::strings(configManager.getConfig().language); }

const char* phaseText(AssistUiPhase phase) {
  switch (phase) {
    case AssistUiPhase::Thinking: return tr().assist_thinking;
    case AssistUiPhase::Answering: return tr().assist_answering;
    case AssistUiPhase::Listening:
    case AssistUiPhase::Idle: break;
  }
  return tr().assist_listening;
}

lv_obj_t* createCircle(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_size(obj, 1, 1);
  return obj;
}

lv_obj_t* createSpinner(lv_obj_t* parent, int32_t width, uint32_t color) {
  lv_obj_t* arc = lv_arc_create(parent);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
  lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
  return arc;
}

// Loudness from a peak sample: dB above a slowly adapting noise floor, so the
// sphere reacts the same at every mic sensitivity and in a noisy room.
float loudness(uint16_t peak) {
  const float db = 20.0f * log10f((peak > 0 ? peak : 1) / 32768.0f);
  if (db < g_floor_db) {
    g_floor_db = db;
  } else {
    g_floor_db += 0.05f;  // ~1.5 dB/s upward drift
  }
  if (g_floor_db < -70.0f) g_floor_db = -70.0f;
  const float level = (db - g_floor_db - 4.0f) / 30.0f;
  return level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
}

void placeCircle(lv_obj_t* obj, float cx, float cy, float diameter) {
  const int32_t d = diameter < 2.0f ? 2 : static_cast<int32_t>(diameter);
  lv_obj_set_size(obj, d, d);
  lv_obj_set_pos(obj, static_cast<int32_t>(cx) - d / 2, static_cast<int32_t>(cy) - d / 2);
}

void onFrame(lv_timer_t*) {
  if (!g_overlay) return;
  const float t = static_cast<float>(lv_tick_elaps(g_started_tick)) / 1000.0f;

  float target_level = 0.0f;
  if (g_shown == AssistUiPhase::Listening) {
    target_level = loudness(speaker::micPeakLevel());
  } else if (g_shown == AssistUiPhase::Answering) {
    target_level = loudness(speaker::outputPeakLevel());
  }
  // Fast attack, slow release: syllables pop, pauses settle.
  g_level += (target_level - g_level) * (target_level > g_level ? 0.55f : 0.12f);
  const float target_mode = g_shown == AssistUiPhase::Listening ? 0.0f : 1.0f;
  g_mode += (target_mode - g_mode) * 0.12f;
  g_appear += (1.0f - g_appear) * 0.18f;

  const int32_t w = lv_obj_get_width(g_overlay);
  const int32_t h = lv_obj_get_height(g_overlay);
  const float cx = w / 2.0f;
  const float cy = h / 2.0f - h * 0.05f;
  const float r = (w < h ? w : h) * 0.17f * g_appear;
  const float m = g_mode;
  const float lv = g_level;

  // Halo: concentric translucent circles that breathe and swell with the
  // level; tinted cyan while listening and violet while thinking.
  const lv_color_t glow = lv_color_mix(lv_color_hex(kGlowThinking), lv_color_hex(kGlowListening),
                                       static_cast<uint8_t>(m * 255.0f));
  for (int i = 0; i < kGlowCount; ++i) {
    const float breathe = 1.0f + 0.04f * sinf(t * 1.6f + i * 0.9f);
    placeCircle(g_glow[i], cx, cy, 2.0f * r * kGlowScale[i] * breathe * (1.0f + 0.28f * lv));
    lv_obj_set_style_bg_color(g_glow[i], glow, 0);
  }

  // Lobes: offset discs that rotate inside the sphere and push outward as the
  // voice gets louder, so the outline morphs; in orbit mode they gather into a
  // smaller, calmer sphere at the center of the orbits.
  const float orbit_r = r * (1.45f + 0.15f * lv * m);
  for (int i = 0; i < kLobeCount; ++i) {
    const float base = i * kTwoPi / kLobeCount;
    const float a_listen = base + t * kLobeSpeed[i];
    const float wobble = 0.6f + 0.4f * sinf(t * 1.7f + i * 2.1f);
    const float off = r * (0.10f + 0.42f * lv) * wobble;
    const float size_listen =
        2.0f * r *
        (0.80f + 0.07f * sinf(t * 2.3f + i * 1.3f) + 0.38f * lv * (0.7f + 0.3f * sinf(t * 9.0f + i * 2.0f)));
    const float off_orbit = r * 0.14f;
    const float size_orbit = r * (1.05f + 0.06f * sinf(t * 2.0f + i * 1.7f) + 0.40f * lv);

    const float x = cx + ((1.0f - m) * off + m * off_orbit) * cosf(a_listen);
    const float y = cy + ((1.0f - m) * off + m * off_orbit) * sinf(a_listen);
    placeCircle(g_lobes[i], x, y, (1.0f - m) * size_listen + m * size_orbit);
  }

  // Core: a bright center that swells with the voice, and pulses (or follows
  // the answer) in orbit mode.
  const float core_listen = 2.0f * r * (0.48f + 0.22f * lv + 0.03f * sinf(t * 3.1f));
  const float core_orbit = r * (0.62f + 0.08f * sinf(t * 4.2f) + 0.30f * lv);
  placeCircle(g_core, cx, cy, (1.0f - m) * core_listen + m * core_orbit);

  // Satellites grow out of the center as the orbit mode fades in.
  const float sat_r = orbit_r * m;
  const float sat_size = r * (0.16f + 0.10f * lv);
  for (int p = 0; p < kOrbitPlanes; ++p) {
    const float ct = cosf(kPlaneTilt[p]);
    const float st = sinf(kPlaneTilt[p]);
    for (int k = 0; k < kSatsPerPlane; ++k) {
      lv_obj_t* sat = g_sats[p * kSatsPerPlane + k];
      if (m < 0.03f) {
        lv_obj_add_flag(sat, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_remove_flag(sat, LV_OBJ_FLAG_HIDDEN);
      // Planes run at slightly different speeds so the pattern keeps changing.
      const float a = t * kOrbitSpeed * (1.0f + 0.15f * p) + k * kTwoPi / kSatsPerPlane + p * 0.6f;
      const float ex = sat_r * cosf(a);
      const float ey = sat_r * kOrbitFlatten * sinf(a);
      const float depth = 0.5f + 0.5f * sinf(a);  // 1 = nearest
      placeCircle(sat, cx + ex * ct - ey * st, cy + ex * st + ey * ct,
                  sat_size * (0.55f + 0.75f * depth));
      lv_obj_set_style_bg_opa(sat, static_cast<lv_opa_t>(m * (90.0f + 165.0f * depth)), 0);
    }
  }

  // HUD rings: a spinner on the outer orbit and a thinner one inside turning
  // the other way, faded in with the orbit.
  const lv_opa_t arc_opa = static_cast<lv_opa_t>(m * 200.0f);
  if (arc_opa < 8) {
    lv_obj_add_flag(g_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_arc_inner, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(g_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_arc_inner, LV_OBJ_FLAG_HIDDEN);
    placeCircle(g_arc, cx, cy, 2.0f * orbit_r + r * 0.35f);
    lv_obj_set_style_arc_opa(g_arc, arc_opa, LV_PART_INDICATOR);
    const int32_t start = static_cast<int32_t>(t * kSpinnerSpeed) % 360;
    const int32_t sweep = 70 + static_cast<int32_t>(40.0f * sinf(t * 1.9f));
    lv_arc_set_angles(g_arc, start, (start + sweep) % 360);
    placeCircle(g_arc_inner, cx, cy, r * 1.75f);
    lv_obj_set_style_arc_opa(g_arc_inner, static_cast<lv_opa_t>(arc_opa * 0.7f), LV_PART_INDICATOR);
    const int32_t inner = 360 - static_cast<int32_t>(t * kSpinnerSpeed * 0.7f) % 360;
    lv_arc_set_angles(g_arc_inner, inner % 360, (inner + 200) % 360);
  }

  // The label sits below the widest halo the sphere reaches.
  lv_obj_set_y(g_label, static_cast<int32_t>(cy + (w < h ? w : h) * 0.17f * 2.1f));
}

void onTap(lv_event_t*) {
  // The user wants the panel back: hide now and stay awake.
  speaker::dismissAssistUi();
  g_woke_display = false;
}

void create() {
  g_overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(g_overlay);
  lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_overlay, lv_color_hex(kBackdrop), 0);
  lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
  lv_obj_remove_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_overlay, onTap, LV_EVENT_CLICKED, nullptr);

  for (int i = kGlowCount - 1; i >= 0; --i) {
    g_glow[i] = createCircle(g_overlay);
    lv_obj_set_style_bg_opa(g_glow[i], kGlowOpa[i], 0);
  }
  for (int i = 0; i < kLobeCount; ++i) {
    g_lobes[i] = createCircle(g_overlay);
    lv_obj_set_style_bg_color(g_lobes[i], lv_color_hex(kLobeColors[i]), 0);
    lv_obj_set_style_bg_grad_color(g_lobes[i], lv_color_hex(kLobeShade[i]), 0);
    lv_obj_set_style_bg_grad_dir(g_lobes[i], LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(g_lobes[i], kLobeOpa, 0);
  }
  g_core = createCircle(g_overlay);
  lv_obj_set_style_bg_color(g_core, lv_color_hex(0xF0FDFF), 0);
  lv_obj_set_style_bg_grad_color(g_core, lv_color_hex(0x67E8F9), 0);
  lv_obj_set_style_bg_grad_dir(g_core, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(g_core, 190, 0);

  g_arc = createSpinner(g_overlay, 3, 0xA5F3FC);
  g_arc_inner = createSpinner(g_overlay, 2, 0xC4B5FD);

  for (int i = 0; i < kSatCount; ++i) {
    g_sats[i] = createCircle(g_overlay);
    lv_obj_set_style_bg_color(g_sats[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_color(g_sats[i], lv_color_hex(kSatColors[i / kSatsPerPlane]), 0);
    lv_obj_set_style_bg_grad_dir(g_sats[i], LV_GRAD_DIR_VER, 0);
    lv_obj_add_flag(g_sats[i], LV_OBJ_FLAG_HIDDEN);
  }

  g_label = lv_label_create(g_overlay);
  lv_obj_remove_flag(g_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(g_label, LV_PCT(100));
  lv_obj_set_style_text_align(g_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(g_label, lv_color_hex(0xC8D2E6), 0);
  lv_obj_set_style_text_font(g_label, &ui_font_28, 0);
  lv_label_set_long_mode(g_label, LV_LABEL_LONG_DOT);

  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
}

void show(AssistUiPhase phase) {
  if (!g_overlay) create();
  g_started_tick = lv_tick_get();
  g_level = 0.0f;
  g_appear = 0.0f;
  g_mode = phase == AssistUiPhase::Listening ? 0.0f : 1.0f;
  g_floor_db = -60.0f;
  lv_obj_remove_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_overlay);
  if (!g_timer) g_timer = lv_timer_create(onFrame, kFramePeriodMs, nullptr);
  onFrame(nullptr);
  Serial.println("[Assist] Screen shown");
}

void hide() {
  if (g_timer) {
    lv_timer_delete(g_timer);
    g_timer = nullptr;
  }
  if (g_overlay) lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  Serial.println("[Assist] Screen hidden");
}

}  // namespace

void service() {
  if (!speaker::supported()) return;
  const AssistUiPhase phase = speaker::assistUiPhase();
  if (phase == AssistUiPhase::Idle) {
    if (g_shown == AssistUiPhase::Idle) return;
    g_shown = AssistUiPhase::Idle;
    hide();
    if (g_woke_display) {
      g_woke_display = false;
      powerManager.enterDisplaySleep();
    }
    return;
  }
  if (g_shown == AssistUiPhase::Idle) show(phase);
  if (phase != g_shown) {
    g_shown = phase;
    lv_label_set_text(g_label, phaseText(phase));
    // A popup opened meanwhile may have been raised above the screen.
    lv_obj_move_foreground(g_overlay);
  }
  // Keep the screensaver and display sleep away while the screen shows.
  const uint32_t now = millis();
  if (now - g_activity_ms >= kActivityRefreshMs) {
    g_activity_ms = now;
    displayManager.resetActivityTimer();
  }
}

bool serviceSleep() {
  if (!speaker::supported() || speaker::assistUiPhase() == AssistUiPhase::Idle) return false;
  g_woke_display = true;
  powerManager.wakeFromDisplaySleep("assist");
  return true;
}

}  // namespace assist_overlay
