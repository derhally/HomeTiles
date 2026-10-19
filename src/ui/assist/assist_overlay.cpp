#include "src/ui/assist/assist_overlay.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>

#include "src/audio/speaker.h"
#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/core/power/power_manager.h"
#include "src/devices/device.h"
#include "src/fonts/ui_fonts.h"
#include "src/ui/assist/assist_screen.h"
#include "src/ui/assist/assist_style_catalog.h"
#include "src/ui/assist/assist_styles.h"

#if HOMETILES_ASSIST_DATA_SCREENS
#include <FS.h>
#include <esp_heap_caps.h>

#include "src/audio/assist_contract.h"
#include "src/ui/assist/assist_screen_files.h"
#include "src/ui/assist/assist_screen_store.h"

// The fonts every panel carries; the engine refuses other sizes.
const lv_font_t* assist_screen_font(int px) {
  switch (px) {
    case 16: return &ui_font_16;
    case 20: return &ui_font_20;
    case 24: return &ui_font_24;
    case 28: return &ui_font_28;
    case 32: return &ui_font_32;
    case 40: return &ui_font_40;
    case 48: return &ui_font_48;
    default: return nullptr;
  }
}
#endif

namespace assist_overlay {
namespace {

using speaker::AssistScreenStyle;
using speaker::AssistUiPhase;

constexpr uint32_t kFramePeriodMs = 33;
constexpr uint32_t kActivityRefreshMs = 1000;
constexpr uint32_t kBackdrop = 0x05070F;
// Preview from Settings: listening, thinking and answering, then it hides.
constexpr uint32_t kPreviewListenMs = 4000;
constexpr uint32_t kPreviewThinkMs = 2500;
constexpr uint32_t kPreviewAnswerMs = 3500;

lv_obj_t* g_overlay = nullptr;
// Holds the current style's objects; cleaned when the style changes.
lv_obj_t* g_stage = nullptr;
lv_obj_t* g_label = nullptr;
lv_timer_t* g_timer = nullptr;
bool g_style_built = false;
AssistScreenStyle g_style = AssistScreenStyle::Atom;

AssistUiPhase g_shown = AssistUiPhase::Idle;
uint32_t g_started_tick = 0;
uint32_t g_activity_ms = 0;
// The display slept when the conversation started; it sleeps again after.
bool g_woke_display = false;
// Settings preview running (millis() start), only while no conversation runs.
bool g_preview = false;
uint32_t g_preview_ms = 0;

// Animation inputs, eased toward their targets every frame.
float g_level = 0.0f;   // voice or answer loudness, 0..1
float g_mode = 0.0f;    // 0 = listening look, 1 = thinking/answering look
float g_appear = 0.0f;  // pop-in scale, 0..1
float g_floor_db = -60.0f;
float g_last_t = 0.0f;  // the previous frame's t, for the styles' dt
// The screen size the offered styles were last worked out for.
int32_t g_offer_w = 0;
int32_t g_offer_h = 0;
bool g_tear_free = false;
// Simulated speech for the preview: syllable bursts with pauses.
float g_sim_target = 0.0f;
uint32_t g_sim_next_ms = 0;

#if HOMETILES_ASSIST_DATA_SCREENS
// Screens from data (assist_screen.h): the chosen style drawn from its screen
// file, /assist/<id>.json on the microSD card or in the internal storage when
// present, else the built-in copy. A file that does not load falls back to the
// compiled style.
constexpr const char* kScreenDir = "/assist/";
assist_screen::Screen* g_screen = nullptr;  // one fixed block, PSRAM, kept
char* g_file_text = nullptr;                // file read buffer, PSRAM, kept
lv_obj_t* g_painter = nullptr;
bool g_data = false;            // the shown style comes from a screen file
bool g_skip_logged = false;     // fill-budget note once per showing

void onPaint(lv_event_t* e) {
  if (!g_data) return;
  lv_area_t coords;
  lv_obj_get_coords(lv_event_get_current_target_obj(e), &coords);
  assist_screen::draw(g_screen, lv_event_get_layer(e), coords.x1, coords.y1);
}

bool ensureScreenMemory() {
  if (g_screen) return true;
  void* memory = heap_caps_malloc(assist_screen::screenSize(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_malloc(assist_screen::screenSize(), MALLOC_CAP_8BIT);
  if (!memory) {
    Serial.printf("[Assist] Screen files off: no memory for %u bytes\n", static_cast<unsigned>(assist_screen::screenSize()));
    return false;
  }
  g_screen = assist_screen::init(memory);
  if (!g_file_text) {
    g_file_text = static_cast<char*>(heap_caps_malloc(assist_screen::kMaxFileBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return true;
}

// The screen file chosen in Home Assistant (assist_screen_store.h); false
// when none is chosen, none was received yet, or it was refused (the reason
// goes to the Assist status).
bool loadChosenFile() {
  const char* id = assist_screen_store::chosen();
  if (!id || !ensureScreenMemory() || !g_file_text) return false;
  size_t length = 0;
  if (!assist_screen_store::readChosen(g_file_text, assist_screen::kMaxFileBytes, &length)) {
    Serial.printf("[Assist] Screen file %s from Home Assistant not received yet, using the built-in style\n", id);
    return false;
  }
  char error[160];
  const uint32_t started = millis();
  if (!assist_screen::load(g_screen, g_file_text, length, error, sizeof(error))) {
    Serial.printf("[Assist] Screen file %s (Home Assistant) refused, using the built-in style: %s\n", id, error);
    assist_screen_store::setError(error);
    return false;
  }
  assist_screen_store::setError("");
  const assist_screen::Info& in = assist_screen::info(g_screen);
  Serial.printf("[Assist] Screen file %s (Home Assistant) loaded in %lu ms: %lu shapes, %lu steps per frame at most\n",
                id, static_cast<unsigned long>(millis() - started), static_cast<unsigned long>(in.frame_cmds),
                static_cast<unsigned long>(in.frame_ops));
  return true;
}

// Loads the screen file for a style; false keeps the compiled style.
bool loadScreenFile(AssistScreenStyle style) {
  if (loadChosenFile()) return true;
  const uint8_t index = static_cast<uint8_t>(style);
  if (index >= assist_contract::kScreenStyleCount) return false;
  const char* id = assist_contract::kScreenStyleIds[index];
  if (!ensureScreenMemory()) return false;
  const char* text = nullptr;
  size_t length = 0;
  const char* source = "built-in";
  char path[48];
  snprintf(path, sizeof(path), "%s%s.json", kScreenDir, id);
  // The card first (uploads through the Web Admin file manager), then the
  // internal storage. sdReadyCached: no card probe in the UI path.
  fs::FS* store = nullptr;
  if (Device::sdReadyCached() && Device::sdFS().exists(path)) {
    store = &Device::sdFS();
    source = "microSD";
  } else if (Device::storageReady() && Device::storageFS().exists(path)) {
    store = &Device::storageFS();
    source = "internal storage";
  }
  if (store) {
    File file = store->open(path, FILE_READ);
    const size_t size = file ? file.size() : 0;
    if (size > 0 && size <= assist_screen::kMaxFileBytes) {
      if (!g_file_text) {
        g_file_text = static_cast<char*>(heap_caps_malloc(assist_screen::kMaxFileBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      }
      if (g_file_text && file.read(reinterpret_cast<uint8_t*>(g_file_text), size) == size) {
        text = g_file_text;
        length = size;
      }
    } else {
      Serial.printf("[Assist] Screen file %s on the %s skipped: %u bytes (limit %u)\n", path, source,
                    static_cast<unsigned>(size), static_cast<unsigned>(assist_screen::kMaxFileBytes));
    }
    if (file) file.close();
    if (!text) source = "built-in";
  }
  if (!text) {
    for (size_t i = 0; i < assist_screen_files::kFileCount; ++i) {
      if (!strcmp(assist_screen_files::kFiles[i].id, id)) {
        text = assist_screen_files::kFiles[i].text;
        length = assist_screen_files::kFiles[i].length;
      }
    }
  }
  if (!text) return false;
  char error[160];
  const uint32_t started = millis();
  if (!assist_screen::load(g_screen, text, length, error, sizeof(error))) {
    Serial.printf("[Assist] Screen file %s (%s) refused, using the built-in style: %s\n", id, source, error);
    return false;
  }
  const assist_screen::Info& in = assist_screen::info(g_screen);
  Serial.printf("[Assist] Screen file %s (%s) loaded in %lu ms: %lu shapes, %lu steps per frame at most\n", id, source,
                static_cast<unsigned long>(millis() - started), static_cast<unsigned long>(in.frame_cmds),
                static_cast<unsigned long>(in.frame_ops));
  return true;
}
#endif

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

AssistUiPhase previewPhase() {
  const uint32_t elapsed = millis() - g_preview_ms;
  if (elapsed < kPreviewListenMs) return AssistUiPhase::Listening;
  if (elapsed < kPreviewListenMs + kPreviewThinkMs) return AssistUiPhase::Thinking;
  if (elapsed < kPreviewListenMs + kPreviewThinkMs + kPreviewAnswerMs) {
    return AssistUiPhase::Answering;
  }
  return AssistUiPhase::Idle;
}

// Loudness from a peak sample: dB above a slowly adapting noise floor, so the
// screen reacts the same at every mic sensitivity and in a noisy room.
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

float simulatedLevel() {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - g_sim_next_ms) >= 0) {
    g_sim_next_ms = now + 110 + lv_rand(0, 170);
    g_sim_target = lv_rand(0, 99) < 22 ? 0.0f : 0.25f + lv_rand(0, 75) / 100.0f;
  }
  return g_sim_target;
}

void onFrame(lv_timer_t*) {
  if (!g_overlay) return;
  const int32_t w = lv_obj_get_width(g_overlay);
  const int32_t h = lv_obj_get_height(g_overlay);
  float base_r = assist_styles::baseRadius(g_style, static_cast<float>(w), static_cast<float>(h));
  float label_r = 2.1f;
#if HOMETILES_ASSIST_DATA_SCREENS
  if (g_data) {
    const assist_screen::Reach& reach =
        assist_screen::info(g_screen).reach[assist_screen::shapeOf(static_cast<float>(w), static_cast<float>(h))];
    base_r = assist_screen::baseRadius(reach, static_cast<float>(w), static_cast<float>(h));
    label_r = reach.label_r;
  }
#endif
  // Without the tear-free path a larger radius would move faster than the
  // tuned ~5 px a frame: the clock runs slower instead.
  const float pace =
      g_tear_free || base_r <= assist_styles::kTunedRadius ? 1.0f : assist_styles::kTunedRadius / base_r;
  const float t = static_cast<float>(lv_tick_elaps(g_started_tick)) / 1000.0f * pace;

  float target_level = 0.0f;
  if (g_shown == AssistUiPhase::Listening) {
    target_level = g_preview ? simulatedLevel() : loudness(speaker::micPeakLevel());
  } else if (g_shown == AssistUiPhase::Answering) {
    target_level = g_preview ? simulatedLevel() : loudness(speaker::outputPeakLevel());
  }
  // Fast attack, slow release: syllables pop, pauses settle.
  g_level += (target_level - g_level) * (target_level > g_level ? 0.55f : 0.12f);
  const float target_mode = g_shown == AssistUiPhase::Listening ? 0.0f : 1.0f;
  g_mode += (target_mode - g_mode) * 0.12f;
  g_appear += (1.0f - g_appear) * 0.18f;

  assist_styles::Frame frame;
  frame.t = t;
  frame.level = g_level;
  frame.mode = g_mode;
  frame.answering = g_shown == AssistUiPhase::Answering;
  frame.cx = w / 2.0f;
  frame.cy = h / 2.0f - h * 0.05f;
  frame.r = base_r * g_appear;
  frame.w = static_cast<float>(w);
  frame.h = static_cast<float>(h);
  const float dt = t - g_last_t;
  frame.dt = dt < 0.0f ? 0.0f : (dt > 0.05f * pace ? 0.05f * pace : dt);
  g_last_t = t;
#if HOMETILES_ASSIST_DATA_SCREENS
  if (g_data) {
    assist_screen::Inputs in{frame.t, frame.dt, frame.level, frame.mode, frame.answering, frame.r,
                             frame.cx, frame.cy, frame.w, frame.h};
    assist_screen::frame(g_screen, in);
    lv_area_t areas[8];
    const int n = assist_screen::dirty(g_screen, areas, 8);
    for (int i = 0; i < n; ++i) lv_obj_invalidate_area(g_painter, &areas[i]);
    if (assist_screen::skippedShapes(g_screen) && !g_skip_logged) {
      g_skip_logged = true;
      Serial.printf("[Assist] Screen file over its fill budget: %lu shapes left out\n",
                    static_cast<unsigned long>(assist_screen::skippedShapes(g_screen)));
    }
  } else {
    assist_styles::frame(g_style, frame);
  }
#else
  assist_styles::frame(g_style, frame);
#endif

  // The label sits below the widest halo the styles reach (a screen file says where).
  lv_obj_set_y(g_label, static_cast<int32_t>(frame.cy + base_r * label_r));
}

void onTap(lv_event_t*) {
  if (g_preview) {
    g_preview = false;
    return;
  }
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

  g_stage = lv_obj_create(g_overlay);
  lv_obj_remove_style_all(g_stage);
  lv_obj_set_size(g_stage, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(g_stage, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(g_stage, LV_OBJ_FLAG_SCROLLABLE);

  g_label = lv_label_create(g_overlay);
  lv_obj_remove_flag(g_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(g_label, LV_PCT(100));
  lv_obj_set_style_text_align(g_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(g_label, lv_color_hex(0xC8D2E6), 0);
  lv_obj_set_style_text_font(g_label, &ui_font_28, 0);
  lv_label_set_long_mode(g_label, LV_LABEL_LONG_DOT);

  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
}

void show(AssistUiPhase phase, AssistScreenStyle style) {
  if (!g_overlay) create();
  // Only the chosen style's objects exist; a new style replaces them.
  bool rebuild = !g_style_built || style != g_style;
#if HOMETILES_ASSIST_DATA_SCREENS
  rebuild = true;  // the screen file is read at every showing (an upload shows next time)
#endif
  if (rebuild) {
    lv_obj_clean(g_stage);
    g_style = style;
    g_style_built = true;
    uint32_t label_color = assist_styles::labelColor(style);
#if HOMETILES_ASSIST_DATA_SCREENS
    g_painter = nullptr;
    g_data = loadScreenFile(style);
    if (g_data) {
      g_painter = lv_obj_create(g_stage);
      lv_obj_remove_style_all(g_painter);
      lv_obj_remove_flag(g_painter, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(g_painter, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(g_painter, LV_PCT(100), LV_PCT(100));
      lv_obj_add_event_cb(g_painter, onPaint, LV_EVENT_DRAW_MAIN, nullptr);
      label_color = assist_screen::info(g_screen).label_color;
    } else {
      assist_styles::create(style, g_stage);
    }
#else
    assist_styles::create(style, g_stage);
#endif
    lv_obj_set_style_text_color(g_label, lv_color_hex(label_color), 0);
  }
#if HOMETILES_ASSIST_DATA_SCREENS
  if (g_data) {
    assist_screen::reset(g_screen, lv_rand(1, 0x7FFFFFFF));
    g_skip_logged = false;
  }
#endif
  g_started_tick = lv_tick_get();
  g_level = 0.0f;
  g_appear = 0.0f;
  g_last_t = 0.0f;
  g_mode = phase == AssistUiPhase::Listening ? 0.0f : 1.0f;
  g_floor_db = -60.0f;
  lv_obj_remove_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_overlay);
  if (!g_timer) g_timer = lv_timer_create(onFrame, kFramePeriodMs, nullptr);
  onFrame(nullptr);
#if HOMETILES_ASSIST_DATA_SCREENS
  Serial.printf("[Assist] Screen shown (style %u, %s)\n", static_cast<unsigned>(style), g_data ? "screen file" : "built-in");
#else
  Serial.printf("[Assist] Screen shown (style %u)\n", static_cast<unsigned>(style));
#endif
}

void hide() {
  if (g_timer) {
    lv_timer_delete(g_timer);
    g_timer = nullptr;
  }
  if (g_overlay) lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  Serial.println("[Assist] Screen hidden");
}

// Works out which styles this panel offers (assist_style_catalog.h) whenever
// the shown screen's size changes, and hands them to the speaker, which lists
// them to Settings and the Bridge.
void offerStyles() {
  lv_display_t* display = lv_display_get_default();
  if (!display) return;
  const int32_t w = lv_display_get_horizontal_resolution(display);
  const int32_t h = lv_display_get_vertical_resolution(display);
  if (w == g_offer_w && h == g_offer_h) return;
  g_offer_w = w;
  g_offer_h = h;
  // The tear-free path only runs in the panel's native orientation.
  g_tear_free = Device::kCapabilities.tear_free_ui && w == Device::kScreenWidth && h == Device::kScreenHeight;
  const uint32_t offered =
      assist_style_catalog::available(assist_style_catalog::features(w, h, g_tear_free));
  speaker::setAvailableAssistScreenStyles(offered);
  Serial.printf("[Assist] Screen styles offered at %ldx%ld%s: 0x%03lX\n", static_cast<long>(w),
                static_cast<long>(h), g_tear_free ? " (tear-free)" : "", static_cast<unsigned long>(offered));
}

}  // namespace

void service() {
  if (!speaker::supported()) return;
  offerStyles();
  AssistUiPhase phase = speaker::assistUiPhase();
  // A real conversation always wins over a preview.
  if (phase != AssistUiPhase::Idle) g_preview = false;
  if (g_preview) {
    phase = previewPhase();
    if (phase == AssistUiPhase::Idle) g_preview = false;
  }
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
  if (g_shown == AssistUiPhase::Idle) show(phase, speaker::assistScreenStyle());
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

void preview() {
  if (!speaker::supported() || speaker::assistUiPhase() != AssistUiPhase::Idle) return;
  // Restart with the stored style, also when a preview is already running.
  if (g_shown != AssistUiPhase::Idle) {
    g_shown = AssistUiPhase::Idle;
    hide();
  }
  g_preview = true;
  g_preview_ms = millis();
  g_sim_next_ms = g_preview_ms;
}

}  // namespace assist_overlay
