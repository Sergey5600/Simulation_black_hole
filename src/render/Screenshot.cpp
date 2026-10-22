#include "render/Screenshot.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "tinyexr.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace bh {

namespace {

void makeParentDirectories(const std::filesystem::path& p) {
  if (!p.has_parent_path()) return;
  std::error_code ec;
  std::filesystem::create_directories(p.parent_path(), ec);
}

}  // namespace

bool writeImage(const std::string& path, int width, int height, const unsigned char* rgb, int channels) {
  const std::filesystem::path p(path);
  makeParentDirectories(p);
  stbi_flip_vertically_on_write(1);
  std::string ext = p.extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (ext == ".jpg" || ext == ".jpeg") return stbi_write_jpg(path.c_str(), width, height, channels, rgb, 95) != 0;
  return stbi_write_png(path.c_str(), width, height, channels, rgb, width * channels) != 0;
}

bool writeExr(const std::string& path, int width, int height, const float* rgba) {
  makeParentDirectories(std::filesystem::path(path));
  std::vector<float> flipped(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  const size_t row = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height; ++y)
    std::copy_n(rgba + static_cast<size_t>(height - 1 - y) * row, row, flipped.begin() + static_cast<std::ptrdiff_t>(y * row));
  const char* err = nullptr;
  if (SaveEXR(flipped.data(), width, height, 4, 1, path.c_str(), &err) != TINYEXR_SUCCESS) {
    if (err) FreeEXRErrorMessage(err);
    return false;
  }
  return true;
}

bool saveScreenshot(const std::string& path, int width, int height) {
  std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  return writeImage(path, width, height, pixels.data());
}

std::string makeTimestampedPath(const char* folder, const char* extension) {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
  return std::string(folder) + "/blackhole_" + stamp + extension;
}

std::string makeScreenshotPath() { return makeTimestampedPath("screenshots", ".png"); }

}  // namespace bh
