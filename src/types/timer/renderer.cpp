#include "src/types/timer/renderer.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/icons/mdi_bar_icons.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/timer/timer_icons.h"
#include "src/ui/shared/tabular_digits.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/ui_theme.h"
#include "src/ui/timers/timer_ui.h"
#include "src/ui/timers/voice_timers.h"

namespace {

using voice_timer::Timer;

constexpr const char* kLogPrefix = "[TimerTile]";
// Every built Timer tile (one per cached grid and folder snapshot).
constexpr size_t kMaxViews = 16;
constexpr uint32_t kPresetMinutes[] = {1, 5, 10, 15};
constexpr size_t kPresetCount = sizeof(kPresetMinutes) / sizeof(kPresetMinutes[0]);
// The other timers a wide tile lists beside the next one.
constexpr size_t kRows = 2;

const i18n::Strings& tr() { return i18n::strings(configManager.getConfig().language); }

struct View {
  lv_obj_t* card = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* value = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* hint = nullptr;
  lv_obj_t* badge = nullptr;
  lv_obj_t* badge_label = nullptr;
  lv_obj_t* pause = nullptr;
  lv_obj_t* pause_icon = nullptr;
  lv_obj_t* add = nullptr;
  lv_obj_t* cancel = nullptr;
  lv_obj_t* presets[kPresetCount] = {};
  lv_obj_t* row_name[kRows] = {};
  lv_obj_t* row_left[kRows] = {};
  String idle_title;
  String idle_icon;
  bool wide = false;
  int content_w = 0;
  int half_w = 0;
  int button = 0;
  int add_w = 0;
  int add_y = 0;
  int center_y = 0;
  int beside = 0;
  uint32_t generation = UINT32_MAX;
  // The shown timer and the listed ones, for the buttons and countdowns.
  char id[voice_timer::kMaxIdChars + 1] = {};
  char row_id[kRows][voice_timer::kMaxIdChars + 1] = {};
  bool active = true;
};

View* g_views[kMaxViews] = {};

void forget_view(lv_event_t* event) {
  View* view = static_cast<View*>(lv_event_get_user_data(event));
  for (View*& slot : g_views) {
    if (slot == view) slot = nullptr;
  }
  delete view;
}

bool remember_view(View* view) {
  for (View*& slot : g_views) {
    if (slot) continue;
    slot = view;
    return true;
  }
  return false;
}

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return obj;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t color) {
  lv_obj_t* obj = lv_label_create(parent);
  set_label_style(obj, lv_color_hex(color), font);
  return obj;
}

int text_width(const lv_font_t* font, const char* text) {
  lv_point_t size{};
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

const lv_font_t* value_font() { return tabular_digits::font(tile_layout::content_font_28()); }

// The tile's buttons: one control step above the card, like the Lock and
// Fan bars' buttons.
lv_obj_t* make_button(View* view, int w, int h, lv_event_cb_t cb, void* user_data) {
  const uint32_t card = lv_color_to_u32(lv_obj_get_style_bg_color(view->card, LV_PART_MAIN)) & 0xFFFFFF;
  lv_obj_t* button = lv_button_create(view->card);
  lv_obj_remove_style_all(button);
  lv_obj_set_size(button, w, h);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(tone_color::lifted(card, card, false, 0.06f)), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(tone_color::lifted(card, card, false, 0.12f)), LV_STATE_PRESSED);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  disable_pressed_button_animation(button);
  lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
  return button;
}

lv_obj_t* button_icon(lv_obj_t* button, const char* icon, int size) {
  lv_obj_t* obj = label(button, mdi_bar_icons::for_bar(size), ui_theme::text());
  lv_label_set_text(obj, getMdiChar(icon).c_str());
  lv_obj_center(obj);
  return obj;
}

lv_obj_t* button_text(lv_obj_t* button, const char* text) {
  lv_obj_t* obj = label(button, tile_layout::header_title_font(), ui_theme::text());
  lv_label_set_text(obj, text);
  lv_obj_center(obj);
  return obj;
}

void on_card(lv_event_t*) { timer_ui::open_popup(); }

void on_preset(lv_event_t* event) {
  const uint32_t minutes = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  voice_timers::requestStart(minutes * 60u);
}

void on_pause(lv_event_t* event) {
  View* view = static_cast<View*>(lv_event_get_user_data(event));
  if (view && view->id[0]) voice_timers::requestAction(view->active ? "pause" : "resume", view->id);
}

void on_add(lv_event_t* event) {
  View* view = static_cast<View*>(lv_event_get_user_data(event));
  if (view && view->id[0]) voice_timers::requestAdd(view->id, 60);
}

void on_cancel(lv_event_t* event) {
  View* view = static_cast<View*>(lv_event_get_user_data(event));
  if (view && view->id[0]) voice_timers::requestAction("cancel", view->id);
}

void set_hidden(lv_obj_t* obj, bool hidden) {
  if (obj) lv_obj_set_flag(obj, LV_OBJ_FLAG_HIDDEN, hidden);
}

void set_text(lv_obj_t* obj, const char* text) {
  if (obj && strcmp(lv_label_get_text(obj), text) != 0) lv_label_set_text(obj, text);
}

void set_icon(View& view, const char* name) {
  if (!view.icon) return;
  String glyph = getMdiChar(name);
  if (!glyph.length()) glyph = getMdiChar(timer_icons::kFallback);
  set_text(view.icon, glyph.c_str());
}

// The icon of a timer: from its name, else the tile's own, else a timer.
void timer_icon(const View& view, const Timer& timer, char* out, size_t size) {
  if (timer_icons::match_any(timer.name, tr().timer_icon_words, i18n::strings("en").timer_icon_words, out, size)) {
    return;
  }
  snprintf(out, size, "%s", view.idle_icon.c_str());
}

const char* timer_name(const View& view, const Timer& timer) {
  if (timer.name[0]) return timer.name;
  return view.idle_title.c_str();
}

// The layout for the list as it is now (a new timer, a pause, one more).
void apply_list(View& view) {
  Timer timer;
  uint32_t left = 0;
  const bool has = voice_timers::nextTimer(&timer, &left);
  const uint8_t count = voice_timers::count();
  view.id[0] = '\0';
  for (auto& id : view.row_id) id[0] = '\0';
  view.active = !has || timer.active;
  const bool many = has && count > 1;

  char text[96];
  if (!has) {
    set_icon(view, view.idle_icon.c_str());
    set_text(view.value, "--:--");
    set_text(view.title, view.idle_title.c_str());
  } else {
    memcpy(view.id, timer.id, sizeof(view.id));
    char icon[timer_icons::kMaxIconChars + 1];
    timer_icon(view, timer, icon, sizeof(icon));
    set_icon(view, icon);
    const char* name = timer_name(view, timer);
    if (!timer.active) {
      snprintf(text, sizeof(text), "%s, %s", name, tr().timer_paused);
    } else if (view.wide && !many) {
      char length[16];
      voice_timer::formatClock(timer.total_s, length, sizeof(length));
      snprintf(text, sizeof(text), tr().timer_of, name, length);
    } else {
      snprintf(text, sizeof(text), "%s", name);
    }
    set_text(view.title, text);
  }
  lv_obj_set_style_text_color(view.value, lv_color_hex(has && timer.active ? ui_theme::text() : ui_theme::text_soft()),
                              0);
  set_hidden(view.hint, has);

  // 1x1: "+2" for more timers.
  if (view.badge) {
    set_hidden(view.badge, !many);
    if (many) {
      snprintf(text, sizeof(text), "+%u", static_cast<unsigned>(count - 1));
      set_text(view.badge_label, text);
    }
  }
  if (!view.wide) return;

  for (lv_obj_t* preset : view.presets) set_hidden(preset, has);
  // Without a timer the name and "No timers running" stand beside the
  // circle; the countdown gives way to the presets.
  set_hidden(view.value, !has);
  const int line = lv_font_get_line_height(tile_layout::header_title_font());
  if (has) {
    lv_obj_align(view.title, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_align(view.value, LV_ALIGN_BOTTOM_LEFT, 0, -line);
  } else {
    lv_obj_set_width(view.title, view.content_w - view.beside);
    lv_obj_align(view.title, LV_ALIGN_TOP_LEFT, view.beside, view.center_y - line);
  }
  set_hidden(view.pause, !has || many);
  set_hidden(view.cancel, !has || many);
  set_hidden(view.add, !has);
  if (has) {
    // One timer: pause, +1 min, cancel at the right; more: +1 min at the
    // right of the left half, the others listed in the right half.
    if (many) {
      lv_obj_align(view.add, LV_ALIGN_TOP_RIGHT, -(view.content_w - view.half_w), view.add_y);
    } else {
      lv_obj_align(view.add, LV_ALIGN_TOP_RIGHT, -(view.button + Device::kGridGap / 2), view.add_y);
      set_text(view.pause_icon, getMdiChar(timer.active ? "pause" : "play").c_str());
    }
  }
  if (has) {
    const int text_w = many ? view.half_w : view.content_w;
    lv_obj_set_width(view.value, text_w);
    lv_obj_set_width(view.title, text_w);
  }

  size_t shown = 0;
  if (many) {
    for (uint8_t i = 0; i < count && shown < kRows; ++i) {
      Timer other;
      uint32_t other_left = 0;
      if (!voice_timers::timerAt(i, &other, &other_left) || strcmp(other.id, timer.id) == 0) continue;
      memcpy(view.row_id[shown], other.id, sizeof(view.row_id[shown]));
      set_text(view.row_name[shown], timer_name(view, other));
      lv_obj_set_style_text_color(view.row_left[shown],
                                  lv_color_hex(other.active ? ui_theme::text() : ui_theme::text_soft()), 0);
      ++shown;
    }
  }
  // From the bottom up: the next one ends up on top.
  const int row_line = line + Device::kGridGap / 2;
  for (size_t i = 0; i < kRows; ++i) {
    const bool on = i < shown;
    set_hidden(view.row_name[i], !on);
    set_hidden(view.row_left[i], !on);
    if (!on) continue;
    const int y = -static_cast<int>(shown - 1 - i) * row_line;
    lv_obj_align(view.row_left[i], LV_ALIGN_BOTTOM_RIGHT, 0, y);
    lv_obj_align(view.row_name[i], LV_ALIGN_BOTTOM_LEFT, view.content_w - view.half_w, y);
  }
}

void apply_countdowns(View& view) {
  char text[16];
  Timer timer;
  uint32_t left = 0;
  if (view.id[0] && voice_timers::nextTimer(&timer, &left)) {
    voice_timer::formatClock(left, text, sizeof(text));
    set_text(view.value, text);
  }
  if (!view.wide || !view.row_id[0][0]) return;
  const uint8_t count = voice_timers::count();
  for (size_t row = 0; row < kRows; ++row) {
    if (!view.row_id[row][0]) continue;
    for (uint8_t i = 0; i < count; ++i) {
      Timer other;
      uint32_t other_left = 0;
      if (!voice_timers::timerAt(i, &other, &other_left) || strcmp(other.id, view.row_id[row]) != 0) continue;
      voice_timer::formatClock(other_left, text, sizeof(text));
      set_text(view.row_left[row], text);
      break;
    }
  }
}

void refresh_view(View& view, bool force) {
  const uint32_t generation = voice_timers::generation();
  if (force || generation != view.generation) {
    view.generation = generation;
    apply_list(view);
  }
  apply_countdowns(view);
}

}  // namespace

lv_obj_t* render_timer_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index) {
  (void)index;
  lv_obj_t* card = lv_button_create(parent);
  if (!card) {
    Serial.printf("%s ERROR: Could not create timer card\n", kLogPrefix);
    return nullptr;
  }
  // The Sensor tile's card.
  const uint32_t card_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  const uint32_t pressed_color = tone_color::pressed_card(card_color);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  const int pad_hor = timer_tile::kPadSide;
  lv_obj_set_style_pad_hor(card, pad_hor, 0);
  lv_obj_set_style_pad_top(card, timer_tile::kPadTop, 0);
  // The countdown and the name sit lower than a Sensor value, clear of the circle.
  lv_obj_set_style_pad_bottom(card, timer_tile::kPadBottom, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  place_tile_card(card, col, row, tile);
  lv_obj_add_event_cb(card, on_card, LV_EVENT_CLICKED, nullptr);

  View* view = new View();
  view->card = card;
  view->idle_title = tile.title.length() ? tile.title : String(tr().timer_default_name);
  const bool icon_off = isMdiIconDisabled(tile.icon_name);
  const String own_icon = normalizeMdiIconName(tile.icon_name);
  view->idle_icon = own_icon.length() ? own_icon : String(timer_icons::kFallback);
  const int card_w = tile_geometry::extent(tile.col, tile.span_w, GRID_CELL_W, GRID_GAP);
  view->content_w = card_w - 2 * pad_hor;
  view->half_w = (view->content_w - Device::kGridGap) / 2;
  view->wide = tile.span_w >= 2 && tile.span_h >= 1;

  if (!icon_off && FONT_MDI_ICONS) {
    view->icon = label(card, FONT_MDI_ICONS, ui_theme::icon());
    lv_label_set_text(view->icon, getMdiChar(view->idle_icon).c_str());
    lv_obj_align(view->icon, LV_ALIGN_TOP_LEFT, tile_layout::scale_480(-8), tile_layout::scale_480(-8));
    tile_icon_source::apply_initial(view->icon, tile);
  }

  const lv_font_t* small = tile_layout::header_title_font();
  view->title = label(card, small, ui_theme::text_soft());
  lv_label_set_long_mode(view->title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(view->title, view->content_w);
  lv_obj_align(view->title, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  const int line = lv_font_get_line_height(small);
  view->value = label(card, value_font(), ui_theme::text());
  lv_label_set_long_mode(view->value, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(view->value, view->content_w);
  lv_obj_align(view->value, LV_ALIGN_BOTTOM_LEFT, 0, -line);
  lv_label_set_text(view->value, "--:--");
  if (view->icon) tile_icon_disc::add_round(card, view->icon);

  // The corner circle (tile_icon_disc::add_round puts it in the corner):
  // the badge and the buttons on its centre line, as high as the circle.
  const timer_tile::Geometry geometry = timer_tile::geometry();
  const int center_y = geometry.center_y;
  view->button = geometry.disc;
  view->add_y = center_y - view->button / 2;
  view->center_y = center_y;
  view->beside = view->icon ? geometry.beside : 0;

  if (!view->wide) {
    const int badge_h = lv_font_get_line_height(small) + Device::kGridGap / 2;
    view->badge = plain(card);
    lv_obj_set_size(view->badge, LV_SIZE_CONTENT, badge_h);
    lv_obj_set_style_min_width(view->badge, badge_h, 0);
    lv_obj_set_style_radius(view->badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(view->badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(view->badge, lv_color_hex(tone_color::lifted(card_color, card_color, false, 0.06f)), 0);
    lv_obj_set_style_pad_hor(view->badge, badge_h / 3, 0);
    lv_obj_align(view->badge, LV_ALIGN_TOP_RIGHT, 0, center_y - badge_h / 2);
    view->badge_label = label(view->badge, small, ui_theme::text());
    lv_obj_center(view->badge_label);
    lv_obj_add_flag(view->badge, LV_OBJ_FLAG_HIDDEN);
    // "Tap to start" under the name when the tile has the height for it.
    const int card_h = tile_geometry::extent(tile.row, tile.span_h, GRID_CELL_H, GRID_GAP);
    if (card_h >= timer_tile::hint_min_height()) {
      view->hint = label(card, small, ui_theme::text_soft());
      lv_label_set_long_mode(view->hint, LV_LABEL_LONG_DOT);
      lv_obj_set_width(view->hint, view->content_w);
      lv_label_set_text(view->hint, tr().timer_tap_start);
      lv_obj_align(view->hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);
      lv_obj_align(view->title, LV_ALIGN_BOTTOM_LEFT, 0, -line);
      lv_obj_align(view->value, LV_ALIGN_BOTTOM_LEFT, 0, -2 * line);
    }
  } else {
    const int gap = Device::kGridGap / 2;
    const int button = view->button;
    view->cancel = make_button(view, button, button, on_cancel, view);
    lv_obj_align(view->cancel, LV_ALIGN_TOP_RIGHT, 0, view->add_y);
    button_icon(view->cancel, "close", button);
    view->add_w = LV_MAX(button, text_width(small, tr().timer_add_minute) + button / 2);
    view->add = make_button(view, view->add_w, button, on_add, view);
    button_text(view->add, tr().timer_add_minute);
    lv_obj_align(view->add, LV_ALIGN_TOP_RIGHT, -(button + gap), view->add_y);
    view->pause = make_button(view, button, button, on_pause, view);
    lv_obj_align(view->pause, LV_ALIGN_TOP_RIGHT, -(button + gap + view->add_w + gap), view->add_y);
    view->pause_icon = button_icon(view->pause, "pause", button);
    // Presets along the bottom without a timer, the name and hint above them.
    const int preset_w = (view->content_w - static_cast<int>(kPresetCount - 1) * gap) / static_cast<int>(kPresetCount);
    char text[24];
    for (size_t i = 0; i < kPresetCount; ++i) {
      lv_obj_t* preset = make_button(view, preset_w, button, on_preset,
                                     reinterpret_cast<void*>(static_cast<uintptr_t>(kPresetMinutes[i])));
      snprintf(text, sizeof(text), tr().timer_minutes, static_cast<int>(kPresetMinutes[i]));
      button_text(preset, text);
      lv_obj_align(preset, LV_ALIGN_BOTTOM_LEFT, static_cast<int>(i) * (preset_w + gap), 0);
      view->presets[i] = preset;
    }
    view->hint = label(card, small, ui_theme::text_soft());
    lv_label_set_long_mode(view->hint, LV_LABEL_LONG_DOT);
    lv_label_set_text(view->hint, tr().timer_none);
    for (size_t i = 0; i < kRows; ++i) {
      view->row_left[i] = label(card, small, ui_theme::text());
      view->row_name[i] = label(card, small, ui_theme::text_soft());
      lv_label_set_long_mode(view->row_name[i], LV_LABEL_LONG_DOT);
      lv_obj_set_width(view->row_name[i], view->half_w - text_width(small, "0:00:00") - gap);
      lv_obj_add_flag(view->row_left[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(view->row_name[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (view->wide) {
    lv_obj_set_width(view->hint, view->content_w - view->beside);
    lv_obj_align(view->hint, LV_ALIGN_TOP_LEFT, view->beside, center_y);
  }

  // The buttons hold the view; it goes with the card.
  lv_obj_add_event_cb(card, forget_view, LV_EVENT_DELETE, view);
  if (!remember_view(view)) Serial.printf("%s No free tile slot; the tile shows the list as it is now\n", kLogPrefix);
  refresh_view(*view, true);
  return card;
}

namespace timer_tile {

void refresh() {
  for (View* view : g_views) {
    if (!view) continue;
    // Later than the render: the colors of the card's surface (tile_surface).
    const ui_theme::SurfaceScope surface = tile_surface::scope(view->card);
    refresh_view(*view, false);
  }
}

}  // namespace timer_tile
