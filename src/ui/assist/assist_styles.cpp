#include "src/ui/assist/assist_styles.h"

#include <math.h>

#include "src/ui/assist/assist_screen.h"
#include "src/ui/assist/assist_styles_shared.h"

namespace assist_styles {
namespace {

using speaker::AssistScreenStyle;

using namespace detail;

// ---------------------------------------------------------------------------
// Atom: cyan, violet and magenta lobes swell with the voice; while thinking
// they gather into a smaller sphere and nine satellites circle it on three
// tilted elliptical orbits inside two counter-turning rings.
// ---------------------------------------------------------------------------
namespace atom {

constexpr int kGlows = 3;
constexpr int kLobes = 3;
constexpr int kPlanes = 3;
constexpr int kSatsPerPlane = 3;
constexpr int kSats = kPlanes * kSatsPerPlane;
constexpr uint32_t kLobeColors[kLobes] = {0x22D3EE, 0x7C3AED, 0xDB2777};
constexpr uint32_t kLobeShade[kLobes] = {0x0E7490, 0x4C1D95, 0x831843};
constexpr lv_opa_t kGlowOpa[kGlows] = {30, 18, 10};
constexpr float kGlowScale[kGlows] = {1.30f, 1.62f, 2.0f};
constexpr uint32_t kGlowListening = 0x0EA5E9;
constexpr uint32_t kGlowThinking = 0x8B5CF6;
// Unequal lobe speeds (rad/s), so the outline never repeats exactly.
constexpr float kLobeSpeed[kLobes] = {0.9f, -1.3f, 1.7f};
constexpr float kOrbitSpeed = 0.9f;      // rad/s
constexpr float kSpinnerSpeed = 100.0f;  // degrees/s
constexpr float kFlatten = 0.34f;        // orbit ellipse height / width
constexpr float kPlaneTilt[kPlanes] = {0.0f, 1.0472f, 2.0944f};  // 0, 60, 120 deg
constexpr uint32_t kSatColors[kPlanes] = {0x67E8F9, 0xC4B5FD, 0xF9A8D4};

lv_obj_t* g_glow[kGlows];
lv_obj_t* g_lobes[kLobes];
lv_obj_t* g_core;
lv_obj_t* g_arc;
lv_obj_t* g_arc_inner;
lv_obj_t* g_sats[kSats];

void create(lv_obj_t* parent) {
  for (int i = kGlows - 1; i >= 0; --i) g_glow[i] = createCircle(parent, kGlowListening, kGlowOpa[i]);
  for (int i = 0; i < kLobes; ++i) g_lobes[i] = createOrb(parent, kLobeColors[i], kLobeShade[i], 150);
  g_core = createOrb(parent, 0xF0FDFF, 0x67E8F9, 190);
  g_arc = createSpinner(parent, 3, 0xA5F3FC);
  g_arc_inner = createSpinner(parent, 2, 0xC4B5FD);
  for (int i = 0; i < kSats; ++i) {
    g_sats[i] = createOrb(parent, 0xFFFFFF, kSatColors[i / kSatsPerPlane], LV_OPA_COVER);
    lv_obj_add_flag(g_sats[i], LV_OBJ_FLAG_HIDDEN);
  }
}

void frame(const Frame& f) {
  const float t = f.t, lv = f.level, m = f.mode, r = f.r, cx = f.cx, cy = f.cy;
  // Halo tinted cyan while listening and violet while thinking.
  const lv_color_t glow = lv_color_mix(lv_color_hex(kGlowThinking), lv_color_hex(kGlowListening),
                                       static_cast<uint8_t>(m * 255.0f));
  for (int i = 0; i < kGlows; ++i) {
    const float breathe = 1.0f + 0.04f * sinf(t * 1.6f + i * 0.9f);
    place(g_glow[i], cx, cy, 2.0f * r * kGlowScale[i] * breathe * (1.0f + 0.28f * lv));
    lv_obj_set_style_bg_color(g_glow[i], glow, 0);
  }
  // Lobes push outward as the voice gets louder, so the outline morphs.
  for (int i = 0; i < kLobes; ++i) {
    const float a = i * kTwoPi / kLobes + t * kLobeSpeed[i];
    const float wobble = 0.6f + 0.4f * sinf(t * 1.7f + i * 2.1f);
    const float off = (1.0f - m) * r * (0.10f + 0.42f * lv) * wobble + m * r * 0.14f;
    const float size_listen =
        2.0f * r *
        (0.80f + 0.07f * sinf(t * 2.3f + i * 1.3f) +
         0.38f * lv * (0.7f + 0.3f * sinf(t * 9.0f + i * 2.0f)));
    const float size_orbit = r * (1.05f + 0.06f * sinf(t * 2.0f + i * 1.7f) + 0.40f * lv);
    place(g_lobes[i], cx + off * cosf(a), cy + off * sinf(a),
          (1.0f - m) * size_listen + m * size_orbit);
  }
  const float core_listen = 2.0f * r * (0.48f + 0.22f * lv + 0.03f * sinf(t * 3.1f));
  const float core_orbit = r * (0.62f + 0.08f * sinf(t * 4.2f) + 0.30f * lv);
  place(g_core, cx, cy, (1.0f - m) * core_listen + m * core_orbit);

  // Satellites grow out of the center as the orbit look fades in; nearer
  // ones (lower half of each ellipse) draw larger and brighter.
  const float orbit_r = r * (1.45f + 0.15f * lv * m);
  const float sat_size = r * (0.16f + 0.10f * lv);
  for (int p = 0; p < kPlanes; ++p) {
    const float ct = cosf(kPlaneTilt[p]);
    const float st = sinf(kPlaneTilt[p]);
    for (int k = 0; k < kSatsPerPlane; ++k) {
      lv_obj_t* sat = g_sats[p * kSatsPerPlane + k];
      if (!visible(sat, m)) continue;
      const float a = t * kOrbitSpeed * (1.0f + 0.15f * p) + k * kTwoPi / kSatsPerPlane + p * 0.6f;
      const float ex = orbit_r * m * cosf(a);
      const float ey = orbit_r * m * kFlatten * sinf(a);
      const float depth = 0.5f + 0.5f * sinf(a);
      place(sat, cx + ex * ct - ey * st, cy + ex * st + ey * ct, sat_size * (0.55f + 0.75f * depth));
      lv_obj_set_style_bg_opa(sat, opa(m * (90.0f + 165.0f * depth) / 255.0f), 0);
    }
  }
  spin(g_arc, cx, cy, 2.0f * orbit_r + r * 0.35f, t * kSpinnerSpeed,
       70.0f + 40.0f * sinf(t * 1.9f), m * 0.8f);
  spin(g_arc_inner, cx, cy, r * 1.75f, -t * kSpinnerSpeed * 0.7f, 200.0f, m * 0.55f);
}

}  // namespace atom

// ---------------------------------------------------------------------------
// Reactor: a bright core sends out sonar rings, faster and wider the louder
// the voice; while thinking, three broken rings turn at different speeds and
// directions like a HUD reticle.
// ---------------------------------------------------------------------------
namespace reactor {

constexpr int kRipples = 4;
constexpr float kRippleLifeS = 1.6f;
struct Segment {
  float radius;  // in r
  float offset;  // degrees
  float sweep;   // degrees
  float speed;   // degrees/s
  int32_t width;
  uint32_t color;
};
constexpr Segment kSegments[] = {
    {1.25f, 0.0f, 70.0f, 45.0f, 3, 0x7DD3FC},   {1.25f, 120.0f, 70.0f, 45.0f, 3, 0x7DD3FC},
    {1.25f, 240.0f, 70.0f, 45.0f, 3, 0x7DD3FC}, {1.55f, 0.0f, 120.0f, -30.0f, 2, 0xA78BFA},
    {1.55f, 180.0f, 120.0f, -30.0f, 2, 0xA78BFA}, {1.85f, 90.0f, 40.0f, 55.0f, 4, 0x7DD3FC},
    {1.85f, 270.0f, 40.0f, 55.0f, 4, 0x7DD3FC},
};
constexpr int kSegmentCount = sizeof(kSegments) / sizeof(kSegments[0]);

lv_obj_t* g_ripples[kRipples];
float g_born[kRipples];
int g_next;
lv_obj_t* g_glow;
lv_obj_t* g_ring;
lv_obj_t* g_core;
lv_obj_t* g_inner;
lv_obj_t* g_segments[kSegmentCount];

void create(lv_obj_t* parent) {
  for (int i = 0; i < kRipples; ++i) {
    g_ripples[i] = createRing(parent, 0x38BDF8, 2);
    lv_obj_add_flag(g_ripples[i], LV_OBJ_FLAG_HIDDEN);
    g_born[i] = -10.0f;
  }
  g_next = 0;
  g_glow = createCircle(parent, 0x0EA5E9, 30);
  g_ring = createRing(parent, 0x7DD3FC, 2);
  g_core = createOrb(parent, 0xE0F2FE, 0x0369A1, LV_OPA_COVER);
  g_inner = createCircle(parent, 0xF0F9FF, 230);
  for (int i = 0; i < kSegmentCount; ++i) {
    g_segments[i] = createSpinner(parent, kSegments[i].width, kSegments[i].color);
  }
}

void frame(const Frame& f) {
  const float t = f.t, lv = f.level, m = f.mode, r = f.r, cx = f.cx, cy = f.cy;
  // Sonar: a new ring every 0.7 s (0.4 s when loud) while listening.
  float newest = -10.0f;
  for (float born : g_born) newest = born > newest ? born : newest;
  if (m < 0.5f && t - newest > 0.7f - 0.3f * lv) {
    g_born[g_next] = t;
    g_next = (g_next + 1) % kRipples;
  }
  for (int i = 0; i < kRipples; ++i) {
    const float life = (t - g_born[i]) / kRippleLifeS;
    const float alpha = life >= 1.0f ? 0.0f : (1.0f - life) * 0.7f * (1.0f - m);
    if (!visible(g_ripples[i], alpha)) continue;
    place(g_ripples[i], cx, cy, 2.0f * r * (0.7f + life * (1.4f + lv)));
    lv_obj_set_style_border_opa(g_ripples[i], opa(alpha), 0);
  }
  place(g_glow, cx, cy, 2.0f * r * (1.6f + 0.4f * lv));
  if (visible(g_ring, 0.5f * (1.0f - m))) {
    place(g_ring, cx, cy, 2.0f * r * (0.95f + 0.25f * lv));
    lv_obj_set_style_border_opa(g_ring, opa(0.5f * (1.0f - m)), 0);
  }
  const float core = (1.0f - m) * 2.0f * r * (0.62f + 0.25f * lv) +
                     m * 2.0f * r * (0.42f + 0.04f * sinf(t * 4.0f) + 0.18f * lv);
  place(g_core, cx, cy, core);
  place(g_inner, cx, cy, core * 0.35f);
  for (int i = 0; i < kSegmentCount; ++i) {
    const Segment& s = kSegments[i];
    spin(g_segments[i], cx, cy, 2.0f * r * s.radius * (1.0f + 0.08f * lv * m) * (0.7f + 0.3f * m),
         s.offset + t * s.speed, s.sweep, m * 0.9f);
  }
}

}  // namespace reactor

// ---------------------------------------------------------------------------
// Dot Halo: 32 dots ring a soft core; the voice pushes each dot outward by a
// different amount, so the ring becomes a living waveform. While thinking a
// bright comet chases around the ring.
// ---------------------------------------------------------------------------
namespace halo {

constexpr int kDots = 32;
constexpr float kChaseSpeed = 1.4f;  // rad/s; the comet only changes opacity

lv_obj_t* g_glow;
lv_obj_t* g_core_glow;
lv_obj_t* g_core;
lv_obj_t* g_dots[kDots];

void create(lv_obj_t* parent) {
  g_glow = createCircle(parent, 0x22D3EE, 20);
  g_core_glow = createCircle(parent, 0x67E8F9, 46);
  g_core = createOrb(parent, 0xECFEFF, 0x0E7490, 220);
  for (int i = 0; i < kDots; ++i) g_dots[i] = createCircle(parent, 0xA5F3FC, LV_OPA_COVER);
}

void frame(const Frame& f) {
  const float t = f.t, lv = f.level, m = f.mode, r = f.r, cx = f.cx, cy = f.cy;
  place(g_glow, cx, cy, 2.0f * r * (1.9f + 0.3f * lv));
  place(g_core_glow, cx, cy, 2.0f * r * (0.85f + 0.2f * lv));
  place(g_core, cx, cy, 2.0f * r * (0.5f + 0.12f * lv + 0.03f * sinf(t * 3.0f)));
  const float ring = r * 1.35f;
  const float head = fmodf(t * kChaseSpeed, kTwoPi);
  for (int i = 0; i < kDots; ++i) {
    const float slot = i * kTwoPi / kDots;
    const float a = slot + t * 0.15f;
    // Per-dot rhythm: a fixed pseudo-random frequency and phase per index.
    const float freq = 2.0f + static_cast<float>((i * 7) % 11) * 0.45f;
    const float phase = static_cast<float>((i * 13) % 17) * 0.37f;
    const float wave = (0.5f + 0.5f * sinf(t * freq + phase)) *
                       (0.6f + 0.4f * sinf(a * 3.0f + t * 2.0f));
    const float push = (1.0f - m) * lv * wave * r * 0.75f + m * lv * r * 0.2f;
    float behind = head - slot;
    if (behind < 0.0f) behind += kTwoPi;
    const float trail = behind < 2.2f ? 1.0f - behind / 2.2f : 0.0f;
    const float alpha = (1.0f - m) * (0.75f + 0.25f * wave * lv) + m * (0.18f + 0.82f * trail);
    lv_obj_set_style_bg_opa(g_dots[i], opa(alpha), 0);
    place(g_dots[i], cx + (ring + push) * cosf(a), cy + (ring + push) * sinf(a),
          r * (0.075f + 0.05f * m * trail + 0.04f * (1.0f - m) * wave * lv));
  }
}

}  // namespace halo

// ---------------------------------------------------------------------------
// Voice Pills: four colored dots stretch into pills that dance with each
// syllable; while thinking they circle each other, and while answering they
// bounce in a wave with the reply.
// ---------------------------------------------------------------------------
namespace pills {

constexpr int kPills = 4;
constexpr uint32_t kColors[kPills] = {0x22D3EE, 0xA78BFA, 0xF472B6, 0xFBBF24};

lv_obj_t* g_glow;
lv_obj_t* g_pills[kPills];
float g_heights[kPills];
float g_answer_mix;

void create(lv_obj_t* parent) {
  g_glow = createCircle(parent, 0x818CF8, 26);
  for (int i = 0; i < kPills; ++i) {
    g_pills[i] = createCircle(parent, kColors[i], LV_OPA_COVER);
    g_heights[i] = 0.0f;
  }
  g_answer_mix = 0.0f;
}

void frame(const Frame& f) {
  const float t = f.t, lv = f.level, m = f.mode, r = f.r, cx = f.cx, cy = f.cy;
  place(g_glow, cx, cy, 2.0f * r * (1.5f + 0.3f * lv));
  g_answer_mix += ((f.answering ? 1.0f : 0.0f) - g_answer_mix) * 0.12f;
  const float dot = r * 0.32f;
  const float gap = r * 0.5f;
  for (int i = 0; i < kPills; ++i) {
    // Listening: each pill takes its own share of the voice.
    const float share = 0.55f + 0.45f * fabsf(sinf(t * (5.0f + i * 1.7f) + i));
    g_heights[i] += (lv * share - g_heights[i]) * 0.5f;
    const float height = (1.0f - m) * (dot + g_heights[i] * r * 1.5f) + m * dot;
    const float row_x = cx + (i - 1.5f) * gap;
    // Thinking: the dots circle the center.
    const float a = t * 2.2f + i * kTwoPi / kPills;
    const float orbit_x = cx + r * 0.55f * cosf(a);
    const float orbit_y = cy + r * 0.55f * sinf(a);
    // Answering: a wave of bounces, higher when the reply is loud.
    const float bounce = fmaxf(0.0f, sinf(t * 7.0f - i * 0.9f)) * r * (0.25f + 0.6f * lv);
    const float think_x = (1.0f - g_answer_mix) * orbit_x + g_answer_mix * row_x;
    const float think_y = (1.0f - g_answer_mix) * orbit_y + g_answer_mix * (cy - bounce);
    placePill(g_pills[i], (1.0f - m) * row_x + m * think_x, (1.0f - m) * cy + m * think_y, dot,
              height);
  }
}

}  // namespace pills

}  // namespace

namespace {

// How far a style reaches sideways from its centre: half_r * r, and for a
// style with fixed-width readouts also side_r * r + side_px. Soft glows that
// fade out may reach further. Screen files state the same (assist_screen.h).
assist_screen::Reach makeReach(float half_r, float side_r = 0.0f, float side_px = 0.0f) {
  assist_screen::Reach out;
  out.half_r = half_r;
  out.side_r = side_r;
  out.side_px = side_px;
  return out;
}

assist_screen::Reach reach(AssistScreenStyle style) {
  switch (style) {
    case AssistScreenStyle::Sentinel: return makeReach(3.95f, 3.27f, 110.0f);  // number left, hex column right
    case AssistScreenStyle::Phosphor: return makeReach(3.55f);                // the scope box
    case AssistScreenStyle::LightDisc: return makeReach(1.75f, 0.0f, 22.0f);  // outer ring and its glow
    case AssistScreenStyle::Lens: return makeReach(1.75f);                    // the plate
    case AssistScreenStyle::Iris: return makeReach(2.2f);                     // the corner brackets
    case AssistScreenStyle::Atom:
    case AssistScreenStyle::Reactor:
    case AssistScreenStyle::DotHalo:
    case AssistScreenStyle::VoicePills: break;
  }
  return makeReach(2.0f);
}

}  // namespace

float baseRadius(AssistScreenStyle style, float w, float h) { return assist_screen::baseRadius(reach(style), w, h); }

void create(AssistScreenStyle style, lv_obj_t* parent) {
  switch (style) {
    case AssistScreenStyle::Reactor: reactor::create(parent); return;
    case AssistScreenStyle::DotHalo: halo::create(parent); return;
    case AssistScreenStyle::VoicePills: pills::create(parent); return;
    // Sentinel, Phosphor, Light Disc, Lens and Iris are screen files
    // (assist_style_catalog::kFileStyles); Atom stands in if one cannot load.
    case AssistScreenStyle::Sentinel:
    case AssistScreenStyle::Phosphor:
    case AssistScreenStyle::LightDisc:
    case AssistScreenStyle::Lens:
    case AssistScreenStyle::Iris:
    case AssistScreenStyle::Atom: break;
  }
  atom::create(parent);
}

void frame(AssistScreenStyle style, const Frame& f) {
  switch (style) {
    case AssistScreenStyle::Reactor: reactor::frame(f); return;
    case AssistScreenStyle::DotHalo: halo::frame(f); return;
    case AssistScreenStyle::VoicePills: pills::frame(f); return;
    case AssistScreenStyle::Sentinel:
    case AssistScreenStyle::Phosphor:
    case AssistScreenStyle::LightDisc:
    case AssistScreenStyle::Lens:
    case AssistScreenStyle::Iris:
    case AssistScreenStyle::Atom: break;
  }
  atom::frame(f);
}

uint32_t labelColor(AssistScreenStyle style) {
  switch (style) {
    case AssistScreenStyle::Sentinel: return 0xE8FBFF;
    case AssistScreenStyle::Phosphor: return 0x39FF7A;
    case AssistScreenStyle::LightDisc: return 0xEFFFFF;
    case AssistScreenStyle::Lens: return 0xB9C0CA;
    case AssistScreenStyle::Iris: return 0xFF9D3C;
    case AssistScreenStyle::Atom:
    case AssistScreenStyle::Reactor:
    case AssistScreenStyle::DotHalo:
    case AssistScreenStyle::VoicePills: break;
  }
  return 0xC8D2E6;
}

}  // namespace assist_styles
