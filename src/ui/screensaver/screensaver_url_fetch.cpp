#include "src/ui/screensaver/screensaver_url_fetch.h"

#include <HTTPClient.h>
#include <Network.h>
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include "src/web/server/web_admin.h"

namespace screensaver_url_fetch {

namespace {

// Expanded templates are at most 512 bytes plus the substituted numbers.
constexpr size_t kMaxUrlBytes = 640;
constexpr size_t kMaxDownloadBytes = 4U * 1024U * 1024U;
constexpr size_t kUnknownLengthBufferBytes = 2U * 1024U * 1024U;
constexpr size_t kReadChunkBytes = 4096;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kIdleTimeoutMs = 8000;
constexpr uint32_t kTotalTimeoutMs = 30000;
// Like media covers, wait for a quiet Web Admin so a download never
// competes with its page and asset transfers on the Wi-Fi link.
constexpr uint32_t kWebAdminQuietMs = 2500;
constexpr uint32_t kMaxWebAdminWaitMs = 15000;

struct Request {
  uint32_t generation;
  char url[kMaxUrlBytes];
};

QueueHandle_t g_requests = nullptr;
QueueHandle_t g_results = nullptr;
TaskHandle_t g_task = nullptr;
volatile bool g_busy = false;

bool network_budget_ok() {
  // TLS needs internal RAM for its record buffers; mirror media covers.
  constexpr size_t kMinInternalFree = 96U * 1024U;
  constexpr size_t kMinInternalLargest = 24U * 1024U;
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
             kMinInternalFree &&
         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                          MALLOC_CAP_8BIT) >=
             kMinInternalLargest;
}

uint8_t* download(const char* url, size_t& out_length) {
  out_length = 0;
  if (!network_budget_ok()) {
    Serial.println("[ScreensaverURL] Skipped: low internal memory");
    return nullptr;
  }
  const bool https = strncmp(url, "https://", 8) == 0;
  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kIdleTimeoutMs);
  http.setReuse(false);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  NetworkClient plain_client;
  NetworkClientSecure secure_client;
  bool begun = false;
  if (https) {
    secure_client.setInsecure();
    begun = http.begin(secure_client, url);
  } else {
    begun = http.begin(plain_client, url);
  }
  if (!begun) {
    Serial.println("[ScreensaverURL] Invalid URL");
    return nullptr;
  }
  http.addHeader("Connection", "close");
  http.addHeader("Accept", "image/jpeg");

  const uint32_t started_ms = millis();
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[ScreensaverURL] HTTP %d\n", code);
    http.end();
    return nullptr;
  }
  const int content_length = http.getSize();
  if (content_length > 0 &&
      static_cast<size_t>(content_length) > kMaxDownloadBytes) {
    Serial.printf("[ScreensaverURL] Image too large: %d bytes\n",
                  content_length);
    http.end();
    return nullptr;
  }
  const size_t capacity = content_length > 0
                              ? static_cast<size_t>(content_length)
                              : kUnknownLengthBufferBytes;
  uint8_t* data = static_cast<uint8_t*>(
      heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!data) {
    Serial.printf("[ScreensaverURL] Buffer allocation failed (%u bytes)\n",
                  static_cast<unsigned>(capacity));
    http.end();
    return nullptr;
  }

  NetworkClient* stream = http.getStreamPtr();
  size_t total = 0;
  uint32_t idle_deadline = millis() + kIdleTimeoutMs;
  bool overflow = false;
  while (stream && millis() - started_ms < kTotalTimeoutMs &&
         static_cast<int32_t>(millis() - idle_deadline) < 0) {
    if (content_length > 0 && total >= static_cast<size_t>(content_length)) {
      break;
    }
    const int available = stream->available();
    if (available > 0) {
      if (total >= capacity) {
        overflow = true;
        break;
      }
      size_t to_read = static_cast<size_t>(available);
      if (to_read > capacity - total) to_read = capacity - total;
      if (to_read > kReadChunkBytes) to_read = kReadChunkBytes;
      const int got = stream->readBytes(data + total, to_read);
      if (got > 0) {
        total += static_cast<size_t>(got);
        idle_deadline = millis() + kIdleTimeoutMs;
      }
      vTaskDelay(1);
    } else {
      if (!http.connected()) break;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  http.end();

  if (overflow) {
    Serial.printf("[ScreensaverURL] Image larger than %u bytes\n",
                  static_cast<unsigned>(capacity));
    free(data);
    return nullptr;
  }
  if (content_length > 0 && total != static_cast<size_t>(content_length)) {
    Serial.printf("[ScreensaverURL] Short download: %u/%d bytes\n",
                  static_cast<unsigned>(total), content_length);
    free(data);
    return nullptr;
  }
  if (total < 32) {
    Serial.printf("[ScreensaverURL] Insufficient data: %u bytes\n",
                  static_cast<unsigned>(total));
    free(data);
    return nullptr;
  }
  Serial.printf("[ScreensaverURL] Downloaded %u bytes in %u ms\n",
                static_cast<unsigned>(total),
                static_cast<unsigned>(millis() - started_ms));
  out_length = total;
  return data;
}

void worker_task(void*) {
  for (;;) {
    Request* request = nullptr;
    if (xQueueReceive(g_requests, &request, portMAX_DELAY) != pdTRUE ||
        !request) {
      continue;
    }
    const uint32_t wait_started_ms = millis();
    while (webAdminRecentlyActive(kWebAdminQuietMs) &&
           millis() - wait_started_ms < kMaxWebAdminWaitMs) {
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    Result result;
    result.generation = request->generation;
    result.data = download(request->url, result.length);
    result.ok = result.data != nullptr;
    free(request);
    if (xQueueSend(g_results, &result, 0) != pdTRUE) {
      // request() never queues while a result is untaken; stay defensive.
      free(result.data);
    }
    g_busy = false;
  }
}

bool ensure_worker() {
  if (!g_requests) g_requests = xQueueCreate(1, sizeof(Request*));
  if (!g_results) g_results = xQueueCreate(1, sizeof(Result));
  if (!g_requests || !g_results) return false;
  if (!g_task) {
    // TLS handshakes need a deep stack, like the media cover worker.
    if (xTaskCreateWithCaps(worker_task, "ss_url", 16384, nullptr, 1, &g_task,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
      g_task = nullptr;
      return false;
    }
  }
  return true;
}

}  // namespace

bool request(const String& url, uint32_t generation) {
  if (g_busy || !url.length() || url.length() >= kMaxUrlBytes) return false;
  if (!ensure_worker()) {
    static uint32_t last_log_ms = 0;
    if (last_log_ms == 0 || millis() - last_log_ms > 30000) {
      last_log_ms = millis() | 1;
      Serial.println("[ScreensaverURL] Worker could not be started");
    }
    return false;
  }
  Result pending;
  if (xQueuePeek(g_results, &pending, 0) == pdTRUE) {
    // A result of the current generation waits for the LVGL timer; one of
    // an older template is stale and only holds PSRAM.
    if (pending.generation == generation) return false;
    if (xQueueReceive(g_results, &pending, 0) == pdTRUE) free(pending.data);
  }
  Request* item = static_cast<Request*>(
      heap_caps_malloc(sizeof(Request), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!item) item = static_cast<Request*>(malloc(sizeof(Request)));
  if (!item) return false;
  item->generation = generation;
  strncpy(item->url, url.c_str(), sizeof(item->url) - 1);
  item->url[sizeof(item->url) - 1] = '\0';
  g_busy = true;
  if (xQueueSend(g_requests, &item, 0) != pdTRUE) {
    g_busy = false;
    free(item);
    return false;
  }
  return true;
}

bool busy() {
  return g_busy;
}

bool take(uint32_t generation, Result& out) {
  out = Result{};
  if (!g_results) return false;
  Result result;
  if (xQueueReceive(g_results, &result, 0) != pdTRUE) return false;
  if (result.generation != generation) {
    free(result.data);
    return false;
  }
  out = result;
  return true;
}

}  // namespace screensaver_url_fetch
