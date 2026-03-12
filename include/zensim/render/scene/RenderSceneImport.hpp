#pragma once
/// @file RenderSceneImport.hpp
/// @brief Bridges zpc's existing MeshIO (OBJ/VTK) into a RenderScene.
///
/// This is a thin adapter: it loads geometry through
/// zs::MeshIO, wraps the result in MeshRef/InstanceRef, and
/// populates a RenderSceneBuilder.

#include "zensim/render/scene/RenderSceneBuilder.hpp"

#include <string>

namespace zs {
namespace render {

  /// Import options for scene loading.
  struct SceneImportOptions {
    /// Auto-generate a default Lambert material if the source
    /// file has no material assignments.
    bool auto_default_material{true};

    /// Auto-add a directional light if no lights are specified.
    bool auto_default_light{true};

    /// Scale factor applied to all vertex positions.
    float uniform_scale{1.f};

    /// If true, re-centre the imported mesh at the origin.
    bool recenter{false};
  };

  /// Load an OBJ file and return a fully-populated RenderScene.
  ///
  /// @param path     Absolute path to the .obj file.
  /// @param name     Scene name (defaults to filename stem).
  /// @param opts     Import options.
  /// @return         A ready-to-render scene (empty on failure).
  RenderScene importOBJ(const std::string& path,
                        const std::string& name = {},
                        const SceneImportOptions& opts = {});

}  // namespace render
}  // namespace zs
