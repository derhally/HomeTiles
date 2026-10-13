#include "src/core/hardware/sar_adc.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace sar_adc {
namespace {

SemaphoreHandle_t mutex() {
  static StaticSemaphore_t buffer;
  static SemaphoreHandle_t handle = xSemaphoreCreateMutexStatic(&buffer);
  return handle;
}

}  // namespace

Lock::Lock() { xSemaphoreTake(mutex(), portMAX_DELAY); }

Lock::~Lock() { xSemaphoreGive(mutex()); }

}  // namespace sar_adc
