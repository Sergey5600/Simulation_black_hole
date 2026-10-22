#include "render/Screenshot.hpp"

#include <glad/gl.h>

#include <ctime>
#include <filesystem>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace bh {

bool saveScreenshot(const std::string& path, int width, int height) {
  std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

  const std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
  }
  stbi_flip_vertically_on_write(1);
  return stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3) != 0;
}

std::string makeScreenshotPath() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char name[64];
  std::strftime(name, sizeof(name), "screenshots/blackhole_%Y%m%d_%H%M%S.png", &tm);
  return name;
}

}  // namespace bh
