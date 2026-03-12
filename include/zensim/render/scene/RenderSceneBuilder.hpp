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

    /// Add a mesh reference (metadata only, no geometry).
    RenderSceneBuilder& addMesh(MeshRef mesh) {
      auto id = static_cast<uint32_t>(mesh.id);
      scene_.mesh_index_[id] = scene_.meshes_.size();
      scene_.meshes_.push_back(std::move(mesh));
      // Keep mesh_data_ in sync — push an empty TriMesh placeholder.
      scene_.mesh_data_.emplace_back();
      return *this;
    }

    /// Add a mesh reference together with its actual triangle data.
    RenderSceneBuilder& addMesh(MeshRef mesh, TriMesh data) {
      auto id = static_cast<uint32_t>(mesh.id);
      scene_.mesh_index_[id] = scene_.meshes_.size();
      scene_.meshes_.push_back(std::move(mesh));
      scene_.mesh_data_.push_back(std::move(data));
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
