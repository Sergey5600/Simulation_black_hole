#pragma once

#include <string>

namespace bh {

// Reads the current default framebuffer and writes it as a PNG. Returns false on failure.
bool saveScreenshot(const std::string& path, int width, int height);

// Writes bottom-up RGB8 pixels (as read from OpenGL). The extension picks the
// format: .jpg/.jpeg (quality 95) or .png. Creates missing directories.
bool writeImage(const std::string& path, int width, int height, const unsigned char* rgb, int channels = 3);

// Writes bottom-up linear RGBA floats as an OpenEXR file (half floats, ZIP),
// top row first as the format expects. Creates missing directories.
bool writeExr(const std::string& path, int width, int height, const float* rgba);

// "screenshots/blackhole_YYYYMMDD_HHMMSS.png" (directory is created on demand).
std::string makeScreenshotPath();

}  // namespace bh
