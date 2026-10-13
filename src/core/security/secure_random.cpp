#include "src/core/security/secure_random.h"

#include <esp_random.h>
#include <soc/soc_caps.h>

#if !SOC_WIFI_SUPPORTED && !SOC_BT_SUPPORTED
#include <bootloader_random.h>

#include "src/core/hardware/sar_adc.h"
#endif

namespace secure_random {

void fill(void* out, size_t length) {
  if (!out || length == 0) return;
#if !SOC_WIFI_SUPPORTED && !SOC_BT_SUPPORTED
  // Without a radio, Espressif documents the RNG as pseudo-random unless the
  // SAR ADC entropy source runs. The shared ADC lock keeps one caller from
  // switching the source off while another still reads, and keeps ADC
  // measurements (which open their driver only inside the lock) out of the
  // entropy window.
  sar_adc::Lock adc_lock;
  bootloader_random_enable();
  esp_fill_random(out, length);
  bootloader_random_disable();
#else
  esp_fill_random(out, length);
#endif
}

}  // namespace secure_random
