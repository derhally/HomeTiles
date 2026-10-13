#include "src/core/hardware/chip_temperature.h"

#include <soc/soc_caps.h>

#if SOC_TEMP_SENSOR_SUPPORTED
#include <driver/temperature_sensor.h>

#include "src/core/hardware/sar_adc.h"
#endif

namespace chip_temperature {

bool read(float* celsius) {
  if (!celsius) return false;
#if SOC_TEMP_SENSOR_SUPPORTED
  // The sensor sits next to the SAR ADC, which secure_random resets on the
  // ESP32-P4, so it is installed and removed inside the shared lock.
  sar_adc::Lock adc_lock;
  temperature_sensor_handle_t sensor = nullptr;
  const temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  if (temperature_sensor_install(&config, &sensor) != ESP_OK) return false;
  bool ok = false;
  if (temperature_sensor_enable(sensor) == ESP_OK) {
    ok = temperature_sensor_get_celsius(sensor, celsius) == ESP_OK;
    temperature_sensor_disable(sensor);
  }
  temperature_sensor_uninstall(sensor);
  return ok;
#else
  return false;
#endif
}

}  // namespace chip_temperature
