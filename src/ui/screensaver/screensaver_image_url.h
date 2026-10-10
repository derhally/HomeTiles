#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Screensaver image URL templates. Pure string helpers without Arduino
// dependencies so the host tests exercise the same code as the firmware.
namespace screensaver_image_url {

constexpr size_t kMaxTemplateBytes = 512;

// True for an empty template (URL source disabled) or a printable http(s)
// URL of at most kMaxTemplateBytes bytes.
bool valid(const std::string& url_template);

// Replaces ${width}, ${height} and ${timestamp} with decimal values. Unknown
// ${...} sequences stay unchanged.
std::string expand(const std::string& url_template, uint32_t width,
                   uint32_t height, uint64_t timestamp);

}  // namespace screensaver_image_url
