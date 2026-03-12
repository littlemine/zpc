#pragma once
/// @file RenderView.hpp
/// @brief A "view" combines a camera with a viewport (resolution,
///        format, MSAA) — one view = one render-target attachment.

#include "zensim/render/RenderTypes.hpp"

#include <string>

namespace zs {
namespace render {

  /// A named (camera + viewport) pair that defines a single output
  /// image for a frame request.
  struct RenderView {
    std::string name{"main"};    ///< Descriptive label (e.g. "main", "shadow_0").
    Camera camera;               ///< View / projection source.
    Viewport viewport;           ///< Resolution & format.
  };

}  // namespace render
}  // namespace zs
