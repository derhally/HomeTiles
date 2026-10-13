#pragma once

namespace chip_temperature {

// Reads the SoC's internal temperature sensor in degrees Celsius. Returns
// false on chips without one or when the sensor could not be read. The
// sensor shares the SAR ADC lock, so do not call this from a hot path.
bool read(float* celsius);

}  // namespace chip_temperature
