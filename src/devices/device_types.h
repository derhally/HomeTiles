#pragma once

#include <Arduino.h>

#include "src/devices/hardware_io_profile.h"

namespace Device {

enum class RotationStepMode : uint8_t {
  QuarterTurns = 0,
  FlipOnly = 1,
};

struct Capabilities {
  bool has_battery;
  bool has_imu;
  bool supports_auto_rotation;
  bool supports_battery_sleep_profile;
  bool supports_usb_host_network;
  bool supports_native_ethernet;
  // Built-in MIPI-CSI camera with a verified board wiring. Profiles that do not
  // list it keep the default.
  bool has_builtin_camera = false;
  // The UI is drawn into the hidden framebuffer and swapped in at the panel's
  // refresh (p4_dsi_camera_presenter setUiFlip), so moving content does not
  // tear. Only while the panel shows its native orientation.
  bool tear_free_ui = false;
};

struct Profile {
  const char* key;
  const char* display_name;
  uint16_t screen_width;
  uint16_t screen_height;
  uint8_t grid_cols;
  uint8_t grid_rows;
  uint16_t grid_gap;
  uint16_t grid_pad;
  uint16_t grid_cell_w;
  uint16_t grid_cell_h;
  uint8_t display_flush_bands;
  // Lowest input value the driver treats as visible 1% brightness. Some P4
  // drivers scale 121..255 to 1..255 internally; direct PWM/M5GFX drivers
  // already use 1..255.
  uint8_t backlight_input_min;
  RotationStepMode rotation_step_mode;
  uint8_t rotation_default;
  uint8_t rotation_flipped;
  Capabilities capabilities;
  HardwareIoProfile hardware_io{};
};

}  // namespace Device
