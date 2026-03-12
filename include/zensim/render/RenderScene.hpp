#pragma once
/// @file RenderScene.hpp
/// @brief CPU-side scene container aggregating meshes, materials,
///        instances and lights for rendering.

#include "zensim/render/RenderTypes.hpp"
#include "zensim/geometry/Mesh.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace zs {
namespace render {

  /// CPU triangle mesh type used for render geometry.
  using TriMesh = zs::Mesh<float, 3, u32, 3>;

  /// A render-facing scene: flat arrays of meshes, materials,
  /// instances and lights.  No GPU resources — just data.
  ///
  /// Construction goes through @ref RenderSceneBuilder (builder
  /// pattern).  Once built the scene is immutable for the duration
  /// of a frame request.
  class RenderScene {
  public:
    // -- accessors --------------------------------------------------

    const std::vector<MeshRef>& meshes() const noexcept { return meshes_; }
    const std::vector<Material>& materials() const noexcept { return materials_; }
    const std::vector<InstanceRef>& instances() const noexcept { return instances_; }
    const std::vector<Light>& lights() const noexcept { return lights_; }

    const MeshRef* findMesh(MeshId id) const noexcept;
    const Material* findMaterial(MaterialId id) const noexcept;
    const InstanceRef* findInstance(SceneObjectId id) const noexcept;

    /// Access the actual CPU triangle mesh data for a given MeshId.
    /// Returns nullptr if the mesh has no geometry data attached.
    const TriMesh* findMeshData(MeshId id) const noexcept;

    bool empty() const noexcept { return instances_.empty(); }
    size_t instanceCount() const noexcept { return instances_.size(); }

    const std::string& name() const noexcept { return name_; }

  private:
    friend class RenderSceneBuilder;

    std::string name_;
    std::vector<MeshRef> meshes_;
    std::vector<TriMesh> mesh_data_;    ///< Parallel to meshes_: actual geometry.
    std::vector<Material> materials_;
    std::vector<InstanceRef> instances_;
    std::vector<Light> lights_;

    // Fast look-ups (populated by the builder).
    std::unordered_map<uint32_t, size_t> mesh_index_;      // MeshId -> index
    std::unordered_map<uint32_t, size_t> material_index_;  // MaterialId -> index
    std::unordered_map<uint32_t, size_t> instance_index_;  // SceneObjectId -> index
  };

  // -----------------------------------------------------------------
  // Inline implementations
  // -----------------------------------------------------------------

  inline const MeshRef* RenderScene::findMesh(MeshId id) const noexcept {
    auto it = mesh_index_.find(static_cast<uint32_t>(id));
    return it != mesh_index_.end() ? &meshes_[it->second] : nullptr;
  }

  inline const Material* RenderScene::findMaterial(MaterialId id) const noexcept {
    auto it = material_index_.find(static_cast<uint32_t>(id));
    return it != material_index_.end() ? &materials_[it->second] : nullptr;
  }

  inline const InstanceRef* RenderScene::findInstance(SceneObjectId id) const noexcept {
    auto it = instance_index_.find(static_cast<uint32_t>(id));
    return it != instance_index_.end() ? &instances_[it->second] : nullptr;
  }

  inline const TriMesh* RenderScene::findMeshData(MeshId id) const noexcept {
    auto it = mesh_index_.find(static_cast<uint32_t>(id));
    if (it == mesh_index_.end() || it->second >= mesh_data_.size()) return nullptr;
    return &mesh_data_[it->second];
  }

}  // namespace render
}  // namespace zs
