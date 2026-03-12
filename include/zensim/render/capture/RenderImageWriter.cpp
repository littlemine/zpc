/// @file RenderImageWriter.cpp
/// @brief PNG writing using zpc's bundled stb_image_write.

// stb_image_write needs the implementation exactly once in one TU.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "zensim/zpc_tpls/stb/stb_image_write.h"

#include "zensim/render/capture/RenderImageWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace zs {
namespace render {

bool writePNG(const std::string& path,
              const uint8_t* pixels,
              uint32_t width, uint32_t height) {
  if (pixels == nullptr || width == 0 || height == 0) return false;

  // stbi_write_png expects: path, w, h, comp, data, stride_in_bytes
  const int stride = static_cast<int>(width) * 4;
  int result = stbi_write_png(path.c_str(),
                              static_cast<int>(width),
                              static_cast<int>(height),
                              4,  // RGBA
                              pixels,
                              stride);
  return result != 0;
}

bool writeDepthPNG(const std::string& path,
                   const float* data,
                   uint32_t width, uint32_t height,
                   float min_val, float max_val) {
  if (data == nullptr || width == 0 || height == 0) return false;

  const size_t pixel_count = static_cast<size_t>(width) * height;
  const float range = (max_val - min_val);
  const float inv_range = (range > 1e-12f) ? (1.f / range) : 0.f;

  // Convert float depth to grayscale R8.
  std::vector<uint8_t> gray(pixel_count);
  for (size_t i = 0; i < pixel_count; ++i) {
    float normalised = (data[i] - min_val) * inv_range;
    normalised = std::fmax(0.f, std::fmin(1.f, normalised));
    gray[i] = static_cast<uint8_t>(normalised * 255.f + 0.5f);
  }

  int result = stbi_write_png(path.c_str(),
                              static_cast<int>(width),
                              static_cast<int>(height),
                              1,  // single channel
                              gray.data(),
                              static_cast<int>(width));
  return result != 0;
}

}  // namespace render
}  // namespace zs
