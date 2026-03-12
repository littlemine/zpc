#pragma once
/// @file RenderImageWriter.hpp
/// @brief Writes pixel data to PNG (or other formats) using
///        zpc's bundled stb_image_write.

#include <cstdint>
#include <string>

namespace zs {
namespace render {

  /// Write RGBA8 pixels to a PNG file.
  /// @param path     Output file path (parent directory must exist).
  /// @param pixels   Row-major RGBA8 pixel data.
  /// @param width    Image width.
  /// @param height   Image height.
  /// @return true on success.
  bool writePNG(const std::string& path,
                const uint8_t* pixels,
                uint32_t width, uint32_t height);

  /// Write a single-channel float buffer as a grayscale PNG
  /// (values are linearly mapped to [0, 255]).
  /// @param path     Output file path.
  /// @param data     Float pixel data (width*height elements).
  /// @param width    Image width.
  /// @param height   Image height.
  /// @param min_val  Value mapped to 0.
  /// @param max_val  Value mapped to 255.
  /// @return true on success.
  bool writeDepthPNG(const std::string& path,
                     const float* data,
                     uint32_t width, uint32_t height,
                     float min_val = 0.f, float max_val = 1.f);

}  // namespace render
}  // namespace zs
