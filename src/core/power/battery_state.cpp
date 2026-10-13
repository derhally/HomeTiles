#include "src/core/power/battery_state.h"

#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)

// Waveshare ESP32-P4-WIFI6-Touch-LCD-7B / 7B-C: the single Li-ion cell on the
// MX1.25 socket (net BAT, charged by an ETA6098) reaches GPIO20 (ADC1)
// through a 200 kOhm / 100 kOhm divider (R92/R93, 100 nF filters), so the pin
// sees one third of the cell voltage. The ETA6098 STAT pin only drives the
// board LED and there is no VBUS sense line, so charging and mains presence
// are not measurable on this board.

namespace {

constexpr uint8_t kBatteryAdcPin = 20;
constexpr int32_t kDividerNumerator = 3;  // (200k + 100k) / 100k
constexpr uint8_t kSamplesPerRead = 16;
constexpr uint32_t kSampleIntervalMs = 1000;
// Outside this window the socket is treated as empty or the reading as bogus.
constexpr int32_t kMinPlausibleMv = 2500;
constexpr int32_t kMaxPlausibleMv = 4500;

// Resting Li-ion voltage to charge mapping. The panel draws current while it
// runs and the charger lifts the voltage while charging, so the percentage is
// an estimate.
struct VoltagePoint {
  int16_t mv;
  int8_t pct;
};
constexpr VoltagePoint kCurve[] = {
  {3300, 0}, {3450, 5}, {3680, 10}, {3740, 20}, {3770, 30}, {3790, 40},
  {3820, 50}, {3870, 60}, {3920, 70}, {4000, 80}, {4100, 90}, {4200, 100},
};

BatteryTelemetry g_state = {
  .on_mains = true,
  .battery_missing = true,
  .level_valid = false,
  .level_pct = -1,
  .raw_level_pct = -1,
  .voltage_mv = 0,
  .current_ma = 0,
  .vbus_mv = -1,
  .has_vbus = false,
  .charging = false,
};
bool g_adc_ready = false;
uint32_t g_last_sample_ms = 0;
float g_filtered_mv = 0.0f;

int32_t percentFromMv(int32_t mv) {
  constexpr size_t kCount = sizeof(kCurve) / sizeof(kCurve[0]);
  if (mv <= kCurve[0].mv) return 0;
  if (mv >= kCurve[kCount - 1].mv) return 100;
  for (size_t i = 1; i < kCount; ++i) {
    if (mv <= kCurve[i].mv) {
      const int32_t span_mv = kCurve[i].mv - kCurve[i - 1].mv;
      const int32_t span_pct = kCurve[i].pct - kCurve[i - 1].pct;
      return kCurve[i - 1].pct + ((mv - kCurve[i - 1].mv) * span_pct + span_mv / 2) / span_mv;
    }
  }
  return 100;
}

int32_t readCellMv() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kSamplesPerRead; ++i) {
    sum += analogReadMilliVolts(kBatteryAdcPin);
  }
  return static_cast<int32_t>(sum / kSamplesPerRead) * kDividerNumerator;
}

}  // namespace

bool batteryStateSupportsMeasurement() { return true; }

void batteryStateUpdate() {
  const uint32_t now_ms = millis();
  if (g_adc_ready && (now_ms - g_last_sample_ms) < kSampleIntervalMs) return;
  g_last_sample_ms = now_ms;

  if (!g_adc_ready) {
    // A full cell gives 1.4 V at the pin; 6 dB covers it with better
    // resolution than the 11/12 dB default.
    analogSetPinAttenuation(kBatteryAdcPin, ADC_6db);
    g_adc_ready = true;
  }

  const int32_t mv = readCellMv();
  if (mv < kMinPlausibleMv || mv > kMaxPlausibleMv) {
    g_state.battery_missing = true;
    g_state.level_valid = false;
    g_state.level_pct = -1;
    g_state.raw_level_pct = -1;
    g_state.voltage_mv = mv;
    g_filtered_mv = 0.0f;
    return;
  }

  // Exponential filter (~10 s at 1 Hz) so load steps from the backlight or
  // Wi-Fi do not make the level jump.
  g_filtered_mv = (g_filtered_mv <= 0.0f) ? static_cast<float>(mv)
                                          : g_filtered_mv + 0.1f * (mv - g_filtered_mv);
  const int32_t filtered_mv = static_cast<int32_t>(g_filtered_mv + 0.5f);
  g_state.battery_missing = false;
  g_state.voltage_mv = filtered_mv;
  g_state.raw_level_pct = percentFromMv(mv);
  g_state.level_pct = percentFromMv(filtered_mv);
  g_state.level_valid = true;
}

const BatteryTelemetry& batteryStateGet() { return g_state; }

// Unknown on this board; keep the mains sleep profile as before.
bool batteryStateIsOnMains() { return true; }

bool batteryStateIsBatteryMissing() { return g_state.battery_missing; }

bool batteryStateHasDisplayPercent() {
  return g_state.level_valid && !g_state.battery_missing;
}

int32_t batteryStateDisplayPercent() {
  return batteryStateHasDisplayPercent() ? g_state.level_pct : -1;
}

int32_t batteryStateRawPercent() {
  return g_state.battery_missing ? -1 : g_state.raw_level_pct;
}

bool batteryStateHasVoltage() {
  return !g_state.battery_missing && g_state.voltage_mv > 0;
}

#else

// The shared telemetry implementation currently returns "on mains, no battery"
// defaults for every profile. Device-specific battery telemetry is not wired here.

static BatteryTelemetry g_stub = {
  .on_mains = true,
  .battery_missing = true,
  .level_valid = false,
  .level_pct = -1,
  .raw_level_pct = -1,
  .voltage_mv = 0,
  .current_ma = 0,
  .vbus_mv = 5000,
  .has_vbus = true,
  .charging = false,
};

bool batteryStateSupportsMeasurement() { return false; }

void batteryStateUpdate() {
  // nothing to poll
}

const BatteryTelemetry& batteryStateGet() {
  return g_stub;
}

bool batteryStateIsOnMains() {
  return true;
}

bool batteryStateIsBatteryMissing() {
  return true;
}

bool batteryStateHasDisplayPercent() {
  return false;
}

int32_t batteryStateDisplayPercent() {
  return -1;
}

int32_t batteryStateRawPercent() {
  return -1;
}

bool batteryStateHasVoltage() {
  return false;
}

#endif
