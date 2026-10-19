// Assist Screen Lab: runs an Assist screen file (FORMAT.md) with the same
// LVGL 9.6 renderer, fonts and frame loop as the panel, in a desktop window
// (SDL2) or headless to PNG files. Built by build.mjs.
//
//   lab <screen.json> [--size=1024x600] [--png=DIR] [--compare] [--frames]
//
// Window keys:
//   1 2 3      listening / thinking / answering     A  auto cycle (default)
//   V          simulated voice on/off (off: hold the mouse, higher = louder)
//   B          show the built-in style with the same id, for comparison
//   [ ]        previous / next panel size           R  reload (also automatic)
//   S          save a screenshot                    Esc quit
//
// The title bar shows the panel-side numbers that matter: shapes and
// expression steps per frame (worst case from the file), pixels redrawn per
// frame, and the host's time per frame (only a rough guide to the panel).

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/assist_screen.h"
#include "src/fonts/ui_fonts.h"
#include "src/ui/assist/assist_styles.cpp"
#include "src/ui/assist/assist_styles_lines.cpp"

#if !defined(LAB_NO_SDL)
#include <SDL.h>
#endif

using speaker::AssistScreenStyle;

// The fonts every panel carries (ui_fonts.h without the 480x480 extras).
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

namespace {

const char* const kStyleIds[] = {"atom", "reactor", "dot_halo", "voice_pills", "sentinel",
                                 "phosphor", "light_disc", "lens", "iris"};
const int kSizes[][2] = {{1024, 600}, {1280, 800}, {800, 480}, {1280, 720}, {720, 720}, {480, 800}, {720, 1280}};
constexpr uint32_t kBackdrop = 0x05070F;
const char* const kPhaseText[3] = {"Listening…", "Thinking…", "Answering…"};

int builtinIndex(const char* id) {
  for (int i = 0; i < 9; ++i) {
    if (!strcmp(kStyleIds[i], id)) return i;
  }
  return -1;
}

// ---- PNG (stored deflate: no zlib needed) ----
uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0) {
  static uint32_t table[256];
  if (!table[1]) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
  }
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void be32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24);
  v.push_back(x >> 16);
  v.push_back(x >> 8);
  v.push_back(x);
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  be32(out, static_cast<uint32_t>(data.size()));
  std::vector<uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());
  be32(out, crc32(body.data(), body.size()));
}

bool writePng(const char* path, int w, int h, const uint32_t* xrgb) {
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(w * 3 + 1) * h);
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    for (int x = 0; x < w; ++x) {
      const uint32_t p = xrgb[y * w + x];
      raw.push_back(p >> 16);
      raw.push_back(p >> 8);
      raw.push_back(p);
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t off = 0; off < raw.size(); off += 65535) {
    const size_t n = raw.size() - off < 65535 ? raw.size() - off : 65535;
    z.push_back(off + n == raw.size() ? 1 : 0);
    z.push_back(n & 0xFF);
    z.push_back(n >> 8);
    z.push_back(~n & 0xFF);
    z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
  }
  be32(z, (b << 16) | a);
  std::vector<uint8_t> out = {137, 80, 78, 71, 13, 10, 26, 10};
  std::vector<uint8_t> ihdr;
  be32(ihdr, w);
  be32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fwrite(out.data(), 1, out.size(), f);
  fclose(f);
  return true;
}

bool readFile(const std::string& path, std::string* out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  out->clear();
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
  fclose(f);
  return true;
}

long mtimeOf(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 ? static_cast<long>(st.st_mtime) : 0;
}

// ---- The panel's frame loop (assist_overlay.cpp onFrame) ----
struct Lab {
  int w = 1024, h = 600;
  std::vector<uint32_t> fb;
  std::vector<uint32_t> band;
  lv_display_t* display = nullptr;
  lv_obj_t* stage = nullptr;    // the file's painter
  lv_obj_t* builtin = nullptr;  // the built-in style's objects
  lv_obj_t* label = nullptr;
  lv_obj_t* message = nullptr;  // load errors
  assist_screen::Screen* screen = nullptr;
  std::vector<uint8_t> memory;
  bool loaded = false;
  bool show_builtin = false;
  int builtin_style = -1;
  std::string path;
  long mtime = 0;
  // Animation inputs, eased like the overlay.
  float level = 0, mode = 0, appear = 0, last_t = 0;
  float sim_target = 0;
  uint32_t sim_next = 0, now = 0, started = 0;
  uint32_t seed = 12345;
  int phase = 0;
  bool simulate = true;
  float manual_level = 0;
  uint64_t redrawn_px = 0;
};

Lab* g_lab = nullptr;

uint32_t rnd(Lab& lab, uint32_t lo, uint32_t hi) {
  lab.seed = lab.seed * 1103515245u + 12345u;
  return lo + (lab.seed >> 8) % (hi - lo + 1);
}

void onPaint(lv_event_t* e) {
  if (!g_lab->loaded || g_lab->show_builtin) return;
  lv_area_t coords;
  lv_obj_get_coords(lv_event_get_current_target_obj(e), &coords);
  assist_screen::draw(g_lab->screen, lv_event_get_layer(e), coords.x1, coords.y1);
}

void onFlush(lv_display_t* d, const lv_area_t* area, uint8_t* data) {
  Lab& lab = *static_cast<Lab*>(lv_display_get_user_data(d));
  auto* source = reinterpret_cast<uint32_t*>(data);
  for (int y = area->y1; y <= area->y2; ++y) {
    for (int x = area->x1; x <= area->x2; ++x) lab.fb[y * lab.w + x] = *source++;
  }
  lab.redrawn_px += static_cast<uint64_t>(lv_area_get_width(area)) * lv_area_get_height(area);
  lv_display_flush_ready(d);
}

float baseRadius(const Lab& lab) {
  if (lab.show_builtin && lab.builtin_style >= 0) {
    return assist_styles::baseRadius(static_cast<AssistScreenStyle>(lab.builtin_style), lab.w, lab.h);
  }
  // assist_styles::baseRadius with the file's reach.
  const assist_screen::Info& in = assist_screen::info(lab.screen);
  float r = (lab.w < lab.h ? lab.w : lab.h) * 0.17f;
  const float half = lab.w / 2.0f - 8.0f;
  r = fminf(r, (half - (in.reach_side_r > 0 ? 0.0f : in.reach_side_px)) / (in.reach_half_r > 0 ? in.reach_half_r : 2.0f));
  if (in.reach_side_r > 0) r = fminf(r, (half - in.reach_side_px) / in.reach_side_r);
  return r < 1 ? 1 : r;
}

void restart(Lab& lab) {
  lab.started = lab.now;
  lab.level = 0;
  lab.appear = 0;
  lab.last_t = 0;
  lab.mode = lab.phase == 0 ? 0.0f : 1.0f;
  if (lab.loaded) assist_screen::reset(lab.screen, 0xC0FFEE);
  if (lab.builtin_style >= 0) {
    lv_obj_clean(lab.builtin);
    assist_styles::create(static_cast<AssistScreenStyle>(lab.builtin_style), lab.builtin);
  }
}

void showMessage(Lab& lab, const char* text) {
  lv_label_set_text(lab.message, text);
  if (text[0]) {
    lv_obj_remove_flag(lab.message, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lab.message, LV_OBJ_FLAG_HIDDEN);
  }
}

bool loadScreen(Lab& lab) {
  std::string text;
  lab.mtime = mtimeOf(lab.path);
  if (!readFile(lab.path, &text)) {
    fprintf(stderr, "Cannot read %s\n", lab.path.c_str());
    showMessage(lab, "Cannot read the screen file");
    return false;
  }
  char error[200];
  const bool ok = assist_screen::load(lab.screen, text.data(), text.size(), error, sizeof(error));
  lab.loaded = ok;
  if (!ok) {
    fprintf(stderr, "%s: %s\n", lab.path.c_str(), error);
    showMessage(lab, error);
    return false;
  }
  const assist_screen::Info& in = assist_screen::info(lab.screen);
  printf("%s: \"%s\" (%s) ok: worst case %lu shapes and %lu expression steps per frame (limits %lu, %lu); "
         "screen memory %lu KB\n",
         lab.path.c_str(), in.name, in.id, static_cast<unsigned long>(in.frame_cmds),
         static_cast<unsigned long>(in.frame_ops), static_cast<unsigned long>(assist_screen::kMaxFrameCmds),
         static_cast<unsigned long>(assist_screen::kMaxFrameOps),
         static_cast<unsigned long>(assist_screen::screenSize() / 1024));
  showMessage(lab, "");
  lab.builtin_style = builtinIndex(in.id);
  lv_obj_set_style_text_color(lab.label, lv_color_hex(in.label_color), 0);
  restart(lab);
  return true;
}

void setSize(Lab& lab, int w, int h) {
  lab.w = w;
  lab.h = h;
  lab.fb.assign(static_cast<size_t>(w) * h, kBackdrop);
  lab.band.assign(static_cast<size_t>(w) * 40, 0);
  lv_display_set_resolution(lab.display, w, h);
  lv_display_set_buffers(lab.display, lab.band.data(), nullptr, lab.band.size() * 4, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_obj_invalidate(lv_screen_active());
  restart(lab);
}

void setup(Lab& lab) {
  lv_init();
  lab.display = lv_display_create(lab.w, lab.h);
  lv_display_set_color_format(lab.display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_user_data(lab.display, &lab);
  lv_display_set_flush_cb(lab.display, onFlush);
  lab.fb.assign(static_cast<size_t>(lab.w) * lab.h, kBackdrop);
  lab.band.assign(static_cast<size_t>(lab.w) * 40, 0);
  lv_display_set_buffers(lab.display, lab.band.data(), nullptr, lab.band.size() * 4, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(kBackdrop), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lab.builtin = lv_obj_create(scr);
  lv_obj_remove_style_all(lab.builtin);
  lv_obj_set_size(lab.builtin, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(lab.builtin, LV_OBJ_FLAG_HIDDEN);
  lab.stage = lv_obj_create(scr);
  lv_obj_remove_style_all(lab.stage);
  lv_obj_set_size(lab.stage, LV_PCT(100), LV_PCT(100));
  lv_obj_add_event_cb(lab.stage, onPaint, LV_EVENT_DRAW_MAIN, nullptr);
  lab.label = lv_label_create(scr);
  lv_obj_set_width(lab.label, LV_PCT(100));
  lv_obj_set_style_text_align(lab.label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(lab.label, &ui_font_28, 0);
  lv_label_set_text(lab.label, kPhaseText[0]);
  lab.message = lv_label_create(scr);
  lv_obj_set_width(lab.message, LV_PCT(96));
  lv_obj_set_pos(lab.message, 10, 10);
  lv_obj_set_style_text_color(lab.message, lv_color_hex(0xFF6B6B), 0);
  lv_obj_set_style_text_font(lab.message, &ui_font_20, 0);
  lv_label_set_long_mode(lab.message, LV_LABEL_LONG_WRAP);
  lv_obj_add_flag(lab.message, LV_OBJ_FLAG_HIDDEN);
  lab.memory.assign(assist_screen::screenSize(), 0);
  lab.screen = assist_screen::init(lab.memory.data());
}

// One 33 ms frame, as the overlay runs it. Returns the host's render time.
double step(Lab& lab) {
  lab.now += 33;
  lv_tick_inc(33);
  const float t = (lab.now - lab.started) / 1000.0f;
  if (lab.now >= lab.sim_next) {
    lab.sim_next = lab.now + 110 + rnd(lab, 0, 170);
    lab.sim_target = rnd(lab, 0, 99) < 22 ? 0.0f : 0.25f + rnd(lab, 0, 75) / 100.0f;
  }
  const float voice = lab.simulate ? lab.sim_target : lab.manual_level;
  const float target = lab.phase == 1 ? 0.0f : voice;
  lab.level += (target - lab.level) * (target > lab.level ? 0.55f : 0.12f);
  lab.mode += ((lab.phase == 0 ? 0.0f : 1.0f) - lab.mode) * 0.12f;
  lab.appear += (1.0f - lab.appear) * 0.18f;
  const float base_r = baseRadius(lab);
  const float dt_raw = t - lab.last_t;
  const float dt = dt_raw < 0 ? 0 : (dt_raw > 0.05f ? 0.05f : dt_raw);
  lab.last_t = t;
  const float cx = lab.w / 2.0f, cy = lab.h / 2.0f - lab.h * 0.05f;
  const auto start = std::chrono::steady_clock::now();
  if (lab.show_builtin && lab.builtin_style >= 0) {
    assist_styles::Frame f{t, lab.level, lab.mode, lab.phase == 2, cx, cy, base_r * lab.appear,
                           static_cast<float>(lab.w), static_cast<float>(lab.h), dt};
    assist_styles::frame(static_cast<AssistScreenStyle>(lab.builtin_style), f);
  } else if (lab.loaded) {
    assist_screen::Inputs in{t, dt, lab.level, lab.mode, lab.phase == 2, base_r * lab.appear, cx, cy,
                             static_cast<float>(lab.w), static_cast<float>(lab.h)};
    assist_screen::frame(lab.screen, in);
    lv_area_t areas[8];
    const int n = assist_screen::dirty(lab.screen, areas, 8);
    for (int i = 0; i < n; ++i) lv_obj_invalidate_area(lab.stage, &areas[i]);
    if (getenv("LAB_DIRTY")) {
      for (int i = 0; i < n; ++i) {
        printf("dirty %d: %d,%d %dx%d\n", i, static_cast<int>(areas[i].x1), static_cast<int>(areas[i].y1),
               static_cast<int>(lv_area_get_width(&areas[i])), static_cast<int>(lv_area_get_height(&areas[i])));
      }
    }
  }
  lv_obj_set_y(lab.label, static_cast<int32_t>(cy + base_r * 2.1f));
  lv_timer_handler();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void setPhase(Lab& lab, int phase) {
  lab.phase = phase;
  lv_label_set_text(lab.label, kPhaseText[phase]);
}

void showBuiltin(Lab& lab, bool on) {
  lab.show_builtin = on && lab.builtin_style >= 0;
  if (lab.show_builtin) {
    lv_obj_remove_flag(lab.builtin, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lab.builtin, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_invalidate(lv_screen_active());
  restart(lab);
}

// ---- Headless: one PNG per phase (file left, built-in right with --compare) ----
int headless(Lab& lab, const std::string& dir, bool compare, bool timing) {
  std::error_code made;
  std::filesystem::create_directories(dir, made);
  if (made) {
    fprintf(stderr, "Cannot create %s: %s\n", dir.c_str(), made.message().c_str());
    return 1;
  }
  const int frames[3] = {150, 90, 120};
  const int shot[3] = {120, 80, 100};
  const char* names[3] = {"listening", "thinking", "answering"};
  const assist_screen::Info& in = assist_screen::info(lab.screen);
  const bool both = compare && lab.builtin_style >= 0;
  std::vector<std::vector<uint32_t>> shots[2];
  double ms[2][3] = {};
  uint64_t px[2][3] = {};
  for (int pass = 0; pass < (both ? 2 : 1); ++pass) {
    showBuiltin(lab, pass == 1);
    lab.now = 0;
    lab.seed = 12345;
    lab.sim_next = 0;
    lv_obj_invalidate(lv_screen_active());
    restart(lab);
    for (int phase = 0; phase < 3; ++phase) {
      setPhase(lab, phase);
      for (int i = 0; i < frames[phase]; ++i) {
        lab.redrawn_px = 0;
        ms[pass][phase] += step(lab);
        px[pass][phase] += lab.redrawn_px;
        if (i == shot[phase]) shots[pass].push_back(lab.fb);
      }
    }
  }
  for (int phase = 0; phase < 3; ++phase) {
    std::vector<uint32_t> out;
    int ow = lab.w;
    if (both) {
      ow = lab.w * 2 + 8;
      out.assign(static_cast<size_t>(ow) * lab.h, 0x303030);
      for (int y = 0; y < lab.h; ++y) {
        memcpy(&out[y * ow], &shots[0][phase][y * lab.w], lab.w * 4);
        memcpy(&out[y * ow + lab.w + 8], &shots[1][phase][y * lab.w], lab.w * 4);
      }
    } else {
      out = shots[0][phase];
    }
    char file[1024];
    snprintf(file, sizeof(file), "%s/%s-%dx%d-%s%s.png", dir.c_str(), in.id, lab.w, lab.h, names[phase],
             both ? "-compare" : "");
    if (!writePng(file, ow, lab.h, out.data())) {
      fprintf(stderr, "Cannot write %s\n", file);
      return 1;
    }
    printf("%s\n", file);
  }
  if (timing) {
    for (int pass = 0; pass < (both ? 2 : 1); ++pass) {
      for (int phase = 0; phase < 3; ++phase) {
        printf("%-8s %-10s %6.2f ms/frame on this computer, %7.0f px redrawn/frame\n", pass ? "built-in" : "file",
               names[phase], ms[pass][phase] / frames[phase],
               static_cast<double>(px[pass][phase]) / frames[phase]);
      }
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Lab lab;
  g_lab = &lab;
  std::string png_dir;
  bool compare = false, timing = false;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (!strncmp(a, "--size=", 7)) {
      if (sscanf(a + 7, "%dx%d", &lab.w, &lab.h) != 2 || lab.w < 100 || lab.h < 100 || lab.w > 2000 || lab.h > 2000) {
        fprintf(stderr, "Bad --size (use e.g. 1024x600)\n");
        return 2;
      }
    } else if (!strncmp(a, "--png=", 6)) {
      png_dir = a + 6;
    } else if (!strcmp(a, "--compare")) {
      compare = true;
    } else if (!strcmp(a, "--frames")) {
      timing = true;
    } else if (a[0] == '-') {
      fprintf(stderr, "Unknown option %s\n", a);
      return 2;
    } else {
      lab.path = a;
    }
  }
  if (lab.path.empty()) {
    fprintf(stderr, "Usage: lab <screen.json> [--size=WxH] [--png=DIR] [--compare] [--frames]\n");
    return 2;
  }
  setup(lab);
  const bool ok = loadScreen(lab);
  if (!png_dir.empty()) return ok ? headless(lab, png_dir, compare, timing) : 1;
#if defined(LAB_NO_SDL)
  fprintf(stderr, "Built without SDL2: use --png=DIR\n");
  return ok ? 0 : 1;
#else
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("Assist Screen Lab", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, lab.w,
                                        lab.h, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
  SDL_Texture* texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, lab.w, lab.h);
  bool running = true, autocycle = true;
  int size_index = 0;
  for (int i = 0; i < 7; ++i) {
    if (kSizes[i][0] == lab.w && kSizes[i][1] == lab.h) size_index = i;
  }
  uint32_t cycle_start = 0, last_title = 0, shot_no = 0;
  double ms_sum = 0;
  uint64_t px_sum = 0;
  int frames = 0;
  while (running) {
    const uint32_t frame_start = SDL_GetTicks();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      if (ev.type == SDL_KEYDOWN) {
        const SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_ESCAPE) running = false;
        if (k >= SDLK_1 && k <= SDLK_3) {
          autocycle = false;
          setPhase(lab, k - SDLK_1);
        }
        if (k == SDLK_a) {
          autocycle = true;
          cycle_start = lab.now;
          setPhase(lab, 0);
          restart(lab);
        }
        if (k == SDLK_v) lab.simulate = !lab.simulate;
        if (k == SDLK_b) showBuiltin(lab, !lab.show_builtin);
        if (k == SDLK_r) loadScreen(lab);
        if (k == SDLK_s) {
          char file[256];
          snprintf(file, sizeof(file), "lab-shot-%u.png", ++shot_no);
          writePng(file, lab.w, lab.h, lab.fb.data());
          printf("Saved %s\n", file);
        }
        if (k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET) {
          size_index = (size_index + (k == SDLK_RIGHTBRACKET ? 1 : 6)) % 7;
          setSize(lab, kSizes[size_index][0], kSizes[size_index][1]);
          SDL_SetWindowSize(window, lab.w, lab.h);
          SDL_DestroyTexture(texture);
          texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, lab.w, lab.h);
        }
      }
    }
    int mx, my;
    const uint32_t buttons = SDL_GetMouseState(&mx, &my);
    lab.manual_level = (buttons & SDL_BUTTON_LMASK) ? 1.0f - static_cast<float>(my) / lab.h : 0.0f;
    // Auto cycle: 5 s listening, 3 s thinking, 4 s answering.
    if (autocycle) {
      const uint32_t at = (lab.now - cycle_start) % 12000;
      setPhase(lab, at < 5000 ? 0 : (at < 8000 ? 1 : 2));
    }
    // Reload when the file changes on disk.
    if (lab.now % 495 == 0 && mtimeOf(lab.path) != lab.mtime) loadScreen(lab);
    lab.redrawn_px = 0;
    ms_sum += step(lab);
    px_sum += lab.redrawn_px;
    ++frames;
    SDL_UpdateTexture(texture, nullptr, lab.fb.data(), lab.w * 4);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    if (SDL_GetTicks() - last_title > 500 && frames) {
      last_title = SDL_GetTicks();
      char title[256];
      const assist_screen::Info& in = assist_screen::info(lab.screen);
      snprintf(title, sizeof(title), "%s %dx%d | %s | %s | %lu shapes max, %lu steps max | %.0f px/frame | %.2f ms/frame here",
               lab.show_builtin ? "BUILT-IN" : (lab.loaded ? in.name : "(error)"), lab.w, lab.h,
               kPhaseText[lab.phase], lab.simulate ? "simulated voice" : "mouse voice",
               static_cast<unsigned long>(in.frame_cmds), static_cast<unsigned long>(in.frame_ops),
               static_cast<double>(px_sum) / frames, ms_sum / frames);
      SDL_SetWindowTitle(window, title);
      ms_sum = 0;
      px_sum = 0;
      frames = 0;
    }
    const uint32_t spent = SDL_GetTicks() - frame_start;
    if (spent < 33) SDL_Delay(33 - spent);
  }
  SDL_Quit();
  return 0;
#endif
}
