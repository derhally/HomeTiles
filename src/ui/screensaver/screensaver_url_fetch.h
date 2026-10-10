#pragma once

#include <Arduino.h>

// One bounded background download for the screensaver image URL. The worker
// never touches LVGL: it only fills a PSRAM buffer that the LVGL timer takes
// and decodes like a microSD file.
namespace screensaver_url_fetch {

struct Result {
  uint32_t generation = 0;
  bool ok = false;
  uint8_t* data = nullptr;  // heap_caps_malloc; the taker frees it
  size_t length = 0;
};

// Queues one download. False while another download or an untaken result is
// still pending, or when the worker cannot be started.
bool request(const String& url, uint32_t generation);

// True while a request is queued or downloading.
bool busy();

// Takes a finished result. Results with another generation are freed and
// reported as not available.
bool take(uint32_t generation, Result& out);

}  // namespace screensaver_url_fetch
