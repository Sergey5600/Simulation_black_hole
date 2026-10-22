#pragma once

#include <string>

namespace bh {

// Reads the current default framebuffer and writes it as a PNG. Returns false on failure.
bool saveScreenshot(const std::string& path, int width, int height);

// "screenshots/blackhole_YYYYMMDD_HHMMSS.png" (directory is created on demand).
std::string makeScreenshotPath();

}  // namespace bh
