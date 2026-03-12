#pragma once
/// @file RenderReadback.hpp
/// @brief Utilities for reading pixel data back from GPU render
///        targets to CPU memory.

#include "zensim/render/RenderTypes.hpp"

#include <cstdint>
#include <vector>

namespace zs {
namespace render {

  /// A CPU-side pixel buffer holding readback data.
  struct ReadbackBuffer {
    std::vector<uint8_t> data;   ///< Raw pixel bytes.
    uint32_t width{0};
    uint32_t height{0};
    uint32_t channels{4};        ///< 4 for RGBA, 1 for depth.
    uint32_t bytes_per_channel{1}; ///< 1 for u8, 4 for f32.

    /// Total byte size.
    size_t byteSize() const noexcept {
      return static_cast<size_t>(width) * height * channels * bytes_per_channel;
    }

    /// Typed access (for depth buffers stored as f32).
    const float* asFloat() const noexcept {
      return reinterpret_cast<const float*>(data.data());
    }
  };

  /// Copy a region of host-visible memory into a ReadbackBuffer.
  /// This is a synchronous helper — the caller must ensure the source
  /// memory is host-visible and the GPU has finished writing.
  ReadbackBuffer createReadback(const void* src,
                                uint32_t width, uint32_t height,
                                uint32_t channels = 4,
                                uint32_t bytes_per_channel = 1);

}  // namespace render
}  // namespace zs
