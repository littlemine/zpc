/// @file RenderReadback.cpp
/// @brief CPU-side readback helper — copies host-visible pixel data
///        into a self-contained ReadbackBuffer.

#include "zensim/render/capture/RenderReadback.hpp"

#include <algorithm>
#include <cstring>

namespace zs {
namespace render {

ReadbackBuffer createReadback(const void* src,
                              uint32_t width, uint32_t height,
                              uint32_t channels,
                              uint32_t bytes_per_channel) {
  ReadbackBuffer buf;
  buf.width = width;
  buf.height = height;
  buf.channels = channels;
  buf.bytes_per_channel = bytes_per_channel;

  const size_t total = buf.byteSize();
  if (total == 0 || src == nullptr) {
    return buf;
  }

  buf.data.resize(total);
  std::memcpy(buf.data.data(), src, total);
  return buf;
}

}  // namespace render
}  // namespace zs
