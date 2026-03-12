#pragma once
/// @file RenderSceneBuilder.hpp
/// @brief Builder for constructing an immutable RenderScene.

#include "zensim/render/RenderScene.hpp"

#include <cassert>
#include <string>
#include <utility>

namespace zs {
namespace render {

  /// Fluent builder for @ref RenderScene.
  ///
  /// Usage:
  /// @code
  ///   auto scene = RenderSceneBuilder("my_scene")
  ///       .addMesh(meshRef)
  ///       .addMaterial(mat)
  ///       .addInstance(inst)
  ///       .addLight(light)
  ///       .build();
  /// @endcode
  class RenderSceneBuilder {
  public:
    explicit RenderSceneBuilder(std::string name = "untitled") {
      scene_.name_ = std::move(name);
    }

    // -- mesh -------------------------------------------------------

    RenderSceneBuilder& addMesh(MeshRef mesh) {
      auto id = static_cast<uint32_t>(mesh.id);
      scene_.mesh_index_[id] = scene_.meshes_.size();
      scene_.meshes_.push_back(std::move(mesh));
      return *this;
    }

    // -- material ---------------------------------------------------

    RenderSceneBuilder& addMaterial(Material mat) {
      auto id = static_cast<uint32_t>(mat.id);
      scene_.material_index_[id] = scene_.materials_.size();
      scene_.materials_.push_back(std::move(mat));
      return *this;
    }

    // -- instance ---------------------------------------------------

    RenderSceneBuilder& addInstance(InstanceRef inst) {
      auto id = static_cast<uint32_t>(inst.id);
      scene_.instance_index_[id] = scene_.instances_.size();
      scene_.instances_.push_back(std::move(inst));
      return *this;
    }

    // -- light ------------------------------------------------------

    RenderSceneBuilder& addLight(Light light) {
      scene_.lights_.push_back(std::move(light));
      return *this;
    }

    // -- finalise ---------------------------------------------------

    /// Consume the builder and return an immutable scene.
    RenderScene build() {
      return std::move(scene_);
    }

  private:
    RenderScene scene_;
  };

}  // namespace render
}  // namespace zs
