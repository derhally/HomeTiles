#include "src/ui/screensaver/screensaver_image_url.h"

namespace screensaver_image_url {

namespace {

bool starts_with(const std::string& text, const char* prefix) {
  return text.rfind(prefix, 0) == 0;
}

}  // namespace

bool valid(const std::string& url_template) {
  if (url_template.empty()) return true;
  if (url_template.size() > kMaxTemplateBytes) return false;
  const bool http = starts_with(url_template, "http://");
  const bool https = starts_with(url_template, "https://");
  if (!http && !https) return false;
  const size_t host_start = http ? 7 : 8;
  if (url_template.size() <= host_start || url_template[host_start] == '/') {
    return false;
  }
  for (const char ch : url_template) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    if (byte <= 0x20 || byte == 0x7F) return false;
  }
  return true;
}

std::string expand(const std::string& url_template, uint32_t width,
                   uint32_t height, uint64_t timestamp) {
  static const struct {
    const char* name;
    size_t length;
  } kVariables[] = {
      {"${width}", 8},
      {"${height}", 9},
      {"${timestamp}", 12},
  };
  const uint64_t values[] = {width, height, timestamp};

  std::string out;
  out.reserve(url_template.size() + 16);
  size_t pos = 0;
  while (pos < url_template.size()) {
    bool replaced = false;
    if (url_template[pos] == '$') {
      for (size_t i = 0; i < sizeof(kVariables) / sizeof(kVariables[0]); ++i) {
        if (url_template.compare(pos, kVariables[i].length,
                                 kVariables[i].name) == 0) {
          out += std::to_string(values[i]);
          pos += kVariables[i].length;
          replaced = true;
          break;
        }
      }
    }
    if (!replaced) out += url_template[pos++];
  }
  return out;
}

}  // namespace screensaver_image_url
