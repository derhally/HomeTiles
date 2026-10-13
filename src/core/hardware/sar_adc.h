#pragma once

namespace sar_adc {

// Serializes every SAR ADC user. On chips without a radio (ESP32-P4)
// secure_random switches the ADC into entropy mode with
// bootloader_random_enable() and resets it afterwards, so an ADC user must
// hold this lock and create and delete its driver inside it; a driver kept
// open across calls would read a reset peripheral. On chips with a radio
// nothing else reconfigures the ADC, but the lock still keeps users apart.
class Lock {
 public:
  Lock();
  ~Lock();
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

}  // namespace sar_adc
