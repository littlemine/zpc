#pragma once
#include <any>
#include <string>

#include "zensim/container/DenseGrid.hpp"
#include "zensim/geometry/SparseLevelSet.hpp"
#include "zensim/math/Vec.h"
#include "zensim/types/Tuple.h"

namespace zs {

  void initialize_openvdb();

  struct OpenVDBStruct {
    OpenVDBStruct() noexcept = default;
    template <typename T> constexpr OpenVDBStruct(T &&obj) : object{FWD(obj)} {}
    template <typename T> T &as() { return std::any_cast<T &>(object); }
    template <typename T> const T &as() const { return std::any_cast<const T &>(object); }
    template <typename T> bool is() const noexcept { return object.type() == typeid(T); }

    std::any object;
  };

  ZPC_API OpenVDBStruct load_floatgrid_from_mesh_file(const std::string &fn, float h);
  ZPC_API OpenVDBStruct load_floatgrid_from_vdb_file(const std::string &fn);
  ZPC_API OpenVDBStruct load_vec3fgrid_from_vdb_file(const std::string &fn);
  ZPC_API bool write_floatgrid_to_vdb_file(std::string_view fn, const OpenVDBStruct &grid);

  /// floatgrid
  template <typename SplsT> OpenVDBStruct convert_sparse_levelset_to_vdbgrid(const SplsT &grid);
  ZPC_API SparseLevelSet<3> convert_floatgrid_to_sparse_levelset(const OpenVDBStruct &grid);
  ZPC_API SparseLevelSet<3> convert_floatgrid_to_sparse_levelset(const OpenVDBStruct &grid,
                                                         const MemoryHandle mh);

  /// float3grid
  ZPC_API SparseLevelSet<3> convert_vec3fgrid_to_sparse_levelset(const OpenVDBStruct &grid);
  ZPC_API SparseLevelSet<3> convert_vec3fgrid_to_sparse_levelset(const OpenVDBStruct &grid,
                                                         const MemoryHandle mh);

  ZPC_API SparseLevelSet<3, grid_e::staggered> convert_vec3fgrid_to_sparse_staggered_grid(
      const OpenVDBStruct &grid);
  ZPC_API SparseLevelSet<3, grid_e::staggered> convert_vec3fgrid_to_sparse_staggered_grid(
      const OpenVDBStruct &grid, const MemoryHandle mh);

  /// floatgrid + float3grid
  ZPC_API SparseLevelSet<3> convert_vdblevelset_to_sparse_levelset(const OpenVDBStruct &sdf,
                                                           const OpenVDBStruct &vel);
  ZPC_API SparseLevelSet<3> convert_vdblevelset_to_sparse_levelset(const OpenVDBStruct &sdf,
                                                           const OpenVDBStruct &vel,
                                                           const MemoryHandle mh);

  void check_floatgrid(OpenVDBStruct &grid);
  OpenVDBStruct particlearray_to_pointdatagrid(const std::vector<std::array<float, 3>> &);
  std::vector<std::array<float, 3>> pointdatagrid_to_particlearray(const OpenVDBStruct &);
  bool write_pointdatagrid_to_file(const OpenVDBStruct &, std::string fn);

}  // namespace zs
