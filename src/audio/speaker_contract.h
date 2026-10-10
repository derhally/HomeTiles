#pragma once

// Pure, host-testable part of the speaker feature: the MQTT contract shared
// with the Home Assistant Bridge (media_player entity), the retained status
// payload, URL validation, volume mapping and the clip format checks used by
// the playback worker. Nothing in this header touches hardware, FreeRTOS or
// Arduino.
//
// MQTT contract (base = the configured device base topic):
//   {base}/cmnd/audio   Bridge -> panel, QoS0, not retained. Sealed on the
//       encrypted command channel (data name "audio") once pairing is active.
//       {"v":1,"op":"play","id":"<16-32 lowercase hex>","url":"http://..."}
//       {"v":1,"op":"stop"}
//       {"v":1,"op":"volume","volume":0.5}        0.0 .. 1.0
//       {"v":1,"op":"mute","muted":true}
//   {base}/stat/audio   panel -> Bridge, QoS0, retained,
//       {"v":1,"state":"idle"|"playing","volume":0.5,"muted":false,
//        "formats":["mp3","wav"],"max_bytes":4194304}
//       plus "id":"<id>" of the current or last clip and "error":"<code>"
//       when that clip failed (cleared by the next play).
//
// The panel downloads the whole clip into PSRAM before playing it, so this
// is for announcements, chimes and TTS, not for endless streams.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace speaker_contract {

constexpr int kProtocolVersion = 1;
constexpr const char* kCommandLeaf = "/cmnd/audio";
constexpr const char* kStatusLeaf = "/stat/audio";
constexpr const char* kSealedDataName = "audio";

constexpr size_t kMinRequestIdLength = 16;
constexpr size_t kMaxRequestIdLength = 32;
constexpr size_t kMaxUrlLength = 1024;
constexpr size_t kMaxRequestPayloadBytes = 1536;
// Largest clip the panel downloads (about four minutes of 128 kbit/s MP3).
constexpr uint32_t kMaxClipBytes = 4u * 1024u * 1024u;
constexpr float kDefaultVolume = 0.5f;
// Volume at 1.0 is 0 dB digital gain; 0.0 mutes. In between the gain falls
// linearly in dB down to -kVolumeRangeDb, which sounds even to the ear.
constexpr float kVolumeRangeDb = 50.0f;
constexpr uint8_t kVolumeRegisterZeroDb = 0xBF;

enum class PublicState : uint8_t { Idle, Playing };

enum class ErrorCode : uint8_t {
  None,
  InvalidUrl,
  HttpsUnsupported,
  DownloadFailed,
  TooLarge,
  UnsupportedFormat,
  DecodeFailed,
  CodecUnavailable,
  OutOfMemory,
};

inline const char* publicStateName(PublicState state) {
  return state == PublicState::Playing ? "playing" : "idle";
}

inline const char* errorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::None: return "";
    case ErrorCode::InvalidUrl: return "invalid_url";
    case ErrorCode::HttpsUnsupported: return "https_unsupported";
    case ErrorCode::DownloadFailed: return "download_failed";
    case ErrorCode::TooLarge: return "too_large";
    case ErrorCode::UnsupportedFormat: return "unsupported_format";
    case ErrorCode::DecodeFailed: return "decode_failed";
    case ErrorCode::CodecUnavailable: return "codec_unavailable";
    case ErrorCode::OutOfMemory: return "out_of_memory";
  }
  return "unknown";
}

inline bool isValidRequestId(const char* id, size_t length) {
  if (!id || length < kMinRequestIdLength || length > kMaxRequestIdLength) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// NaN and out-of-range values clamp; the Bridge sends 0.0 .. 1.0.
inline float clampVolume(double volume) {
  if (!(volume == volume)) return kDefaultVolume;
  if (volume <= 0.0) return 0.0f;
  if (volume >= 1.0) return 1.0f;
  return static_cast<float>(volume);
}

// ES8311 DAC volume register 0x32 (0.5 dB steps, 0xBF = 0 dB, 0 = mute).
inline uint8_t volumeRegister(float volume) {
  const float clamped = clampVolume(volume);
  if (clamped <= 0.0f) return 0;
  const float db = -kVolumeRangeDb * (1.0f - clamped);
  const long steps = lroundf(db * 2.0f);
  const long value = static_cast<long>(kVolumeRegisterZeroDb) + steps;
  return static_cast<uint8_t>(value < 1 ? 1 : value);
}

// Absolute http:// or https:// URL with a host, printable ASCII without
// spaces. A caller may refuse https:// (https_allowed = false).
inline ErrorCode validateUrl(const char* url, size_t length, bool https_allowed) {
  if (!url || length == 0 || length > kMaxUrlLength) return ErrorCode::InvalidUrl;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(url[i]);
    if (c <= 0x20 || c >= 0x7F) return ErrorCode::InvalidUrl;
  }
  size_t host = 0;
  if (length > 7 && strncmp(url, "http://", 7) == 0) {
    host = 7;
  } else if (length > 8 && strncmp(url, "https://", 8) == 0) {
    if (!https_allowed) return ErrorCode::HttpsUnsupported;
    host = 8;
  } else {
    return ErrorCode::InvalidUrl;
  }
  if (url[host] == '/' || url[host] == '?' || url[host] == '#') return ErrorCode::InvalidUrl;
  return ErrorCode::None;
}

enum class ClipFormat : uint8_t { Unknown, Mp3, Wav };

inline uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// By content, never by URL or Content-Type: TTS proxies often send neither.
inline ClipFormat detectFormat(const uint8_t* data, size_t length) {
  if (!data) return ClipFormat::Unknown;
  if (length >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0) {
    return ClipFormat::Wav;
  }
  if (length >= 3 && memcmp(data, "ID3", 3) == 0) return ClipFormat::Mp3;
  // MPEG audio frame sync (11 set bits), layer III.
  if (length >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0 && (data[1] & 0x06) == 0x02) {
    return ClipFormat::Mp3;
  }
  return ClipFormat::Unknown;
}

struct WavInfo {
  uint32_t sample_rate = 0;
  uint16_t channels = 0;
  size_t data_offset = 0;
  size_t data_length = 0;
};

// 16-bit PCM (format 1, or WAVE_FORMAT_EXTENSIBLE with the PCM sub-format),
// mono or stereo. A data chunk longer than the file is cut to what arrived.
inline bool parseWav(const uint8_t* data, size_t length, WavInfo* out) {
  if (!out || detectFormat(data, length) != ClipFormat::Wav) return false;
  *out = WavInfo{};
  bool have_fmt = false;
  size_t pos = 12;
  while (pos + 8 <= length) {
    const uint8_t* chunk = data + pos;
    const uint32_t size = readLe32(chunk + 4);
    const size_t body = pos + 8;
    if (memcmp(chunk, "fmt ", 4) == 0) {
      if (size < 16 || body + 16 > length) return false;
      uint16_t format = readLe16(data + body);
      if (format == 0xFFFE) {
        if (size < 40 || body + 26 > length) return false;
        format = readLe16(data + body + 24);
      }
      const uint16_t channels = readLe16(data + body + 2);
      const uint32_t rate = readLe32(data + body + 4);
      const uint16_t bits = readLe16(data + body + 14);
      if (format != 1 || bits != 16 || channels < 1 || channels > 2 || rate == 0) return false;
      out->channels = channels;
      out->sample_rate = rate;
      have_fmt = true;
    } else if (memcmp(chunk, "data", 4) == 0) {
      if (!have_fmt) return false;
      out->data_offset = body;
      const size_t available = length - body;
      out->data_length = size < available ? size : available;
      out->data_length -= out->data_length % (2u * out->channels);
      return out->data_length > 0;
    }
    // Chunks are padded to an even size.
    const uint64_t next = static_cast<uint64_t>(body) + size + (size & 1u);
    if (next > length) return false;
    pos = static_cast<size_t>(next);
  }
  return false;
}

// In place: interleaved stereo frames to mono (the speaker has one channel).
inline void downmixToMono(int16_t* samples, size_t frames) {
  for (size_t i = 0; i < frames; ++i) {
    const int32_t sum = static_cast<int32_t>(samples[2 * i]) + samples[2 * i + 1];
    samples[i] = static_cast<int16_t>(sum / 2);
  }
}

struct StatusFields {
  PublicState state = PublicState::Idle;
  float volume = kDefaultVolume;
  bool muted = false;
  const char* id = nullptr;
  ErrorCode error = ErrorCode::None;
};

// Returns the JSON length, or 0 when the buffer is too small.
inline size_t buildStatusJson(char* out, size_t size, const StatusFields& fields) {
  if (!out || size == 0) return 0;
  const float volume = clampVolume(fields.volume);
  int written = snprintf(out, size,
                         "{\"v\":%d,\"state\":\"%s\",\"volume\":%.2f,\"muted\":%s,"
                         "\"formats\":[\"mp3\",\"wav\"],\"max_bytes\":%lu",
                         kProtocolVersion, publicStateName(fields.state),
                         static_cast<double>(volume), fields.muted ? "true" : "false",
                         static_cast<unsigned long>(kMaxClipBytes));
  if (written < 0 || static_cast<size_t>(written) >= size) return 0;
  size_t used = static_cast<size_t>(written);
  if (fields.id && isValidRequestId(fields.id, strlen(fields.id))) {
    written = snprintf(out + used, size - used, ",\"id\":\"%s\"", fields.id);
    if (written < 0 || static_cast<size_t>(written) >= size - used) return 0;
    used += static_cast<size_t>(written);
  }
  if (fields.error != ErrorCode::None) {
    written = snprintf(out + used, size - used, ",\"error\":\"%s\"", errorCodeName(fields.error));
    if (written < 0 || static_cast<size_t>(written) >= size - used) return 0;
    used += static_cast<size_t>(written);
  }
  if (used + 2 > size) return 0;
  out[used++] = '}';
  out[used] = '\0';
  return used;
}

}  // namespace speaker_contract
