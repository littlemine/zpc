#pragma once
#include "AdaptiveGrid.hpp"
#include "zensim/execution/ExecutionPolicy.hpp"
#include "zensim/math/Vec.h"

namespace zs {

  // ============================================================================
  //
  //  AdaptiveGrid Algorithms
  //
  //  OpenVDB-style: CSG, flood fill, topology ops, differential operators
  //  Bifrost-style: adaptive refinement, coarsening, prolongation, restriction
  //
  // ============================================================================

  // ============================================================================
  //
  //  Topology Operations
  //
  // ============================================================================

  /// @brief Fill all active voxels at a specific level with a constant value
  /// @tparam LevelNo The tree level to operate on (0 = leaf)
  template <int LevelNo = 0> struct AgFillLevel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, chn, val] = params;
      auto &lev = agv.level(dim_c<LevelNo>);
      constexpr auto block_size = RM_CVREF_T(lev)::block_size;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (lev.valueMask[bno].isOn(cno)) {
        lev.grid(chn, bno, cno) = val;
      }
    }
  };

  /// @brief Fill all active voxels across all levels with a constant value
  namespace detail {
    template <int L, int NumLevels, typename ExecPol, typename AdaptiveGridT, typename AgvT,
              execspace_e space>
    void ag_fill_level(ExecPol &&pol, AdaptiveGridT &grid, AgvT &agv, size_t chn,
                       typename AdaptiveGridT::value_type val) {
      auto &lev = grid.level(dim_c<L>);
      auto nbs = lev.numBlocks();
      if (nbs > 0) {
        constexpr auto bs = AdaptiveGridT::template get_tile_size<L>();
        auto params
            = zs::make_tuple(agv, (typename AdaptiveGridT::size_type)chn, val);
        pol(range(nbs * bs), params, AgFillLevel<L>{});
      }
      if constexpr (L + 1 < NumLevels)
        ag_fill_level<L + 1, NumLevels, ExecPol, AdaptiveGridT, AgvT, space>(
            FWD(pol), grid, agv, chn, val);
    }
  }  // namespace detail

  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_fill(ExecPol &&pol, AdaptiveGridT &grid, size_t chn,
               typename AdaptiveGridT::value_type val) {
    auto agv = view<space>(grid);
    detail::ag_fill_level<0, AdaptiveGridT::num_levels, ExecPol, AdaptiveGridT, decltype(agv),
                          space>(FWD(pol), grid, agv, chn, val);
  }

  /// @brief Topology dilation kernel — for each active voxel at the leaf level,
  /// ensure face-adjacent neighbor blocks exist in the hash table
  struct AgDilateLeafKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, newTab] = params;
      auto &lev = agv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(lev)::block_size;
      constexpr int dim = RM_CVREF_T(agv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (lev.valueMask[bno].isOff(cno)) return;
      auto coord = lev.table._activeKeys[bno]
                   + RM_CVREF_T(agv)::template tile_offset_to_coord<0>(cno);
      // Check face neighbors
      for (int axis = 0; axis < dim; ++axis) {
        for (int dir = -1; dir <= 1; dir += 2) {
          auto neighborCoord = coord;
          neighborCoord[axis] += dir;
          auto key = RM_CVREF_T(agv)::template coord_to_key<0>(neighborCoord);
          newTab.insert(key);
        }
      }
    }
  };

  /// @brief Dilate the active topology at leaf level by one voxel in each direction.
  /// After dilation, new blocks are allocated and initialized with background values.
  /// The topology is re-complemented to maintain tree consistency.
  /// @param iterations Number of dilation iterations
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_dilate(ExecPol &&pol, AdaptiveGridT &grid, int iterations = 1) {
    for (int iter = 0; iter < iterations; ++iter) {
      auto &lev0 = grid.level(dim_c<0>);
      auto nbs = lev0.numBlocks();
      if (nbs == 0) return;
      constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

      // Reserve space for expanded partition
      auto prevNbs = nbs;
      // Worst case: each voxel could spawn 2*dim new blocks
      lev0.resizePartition(pol, prevNbs + prevNbs);

      auto agv = view<space>(grid);
      auto params = zs::make_tuple(agv, view<space>(lev0.table));
      pol(range(prevNbs * bs), params, AgDilateLeafKernel{});

      auto newNbs = lev0.numBlocks();
      if (newNbs > prevNbs) {
        lev0.refitToPartition();
        // Zero-initialize only the newly added blocks (not the existing ones)
        for (auto bi = prevNbs; bi < newNbs; ++bi) {
          lev0.valueMask[bi].setOff();
          lev0.childMask[bi].setOff();
        }
        auto gridV = view<space>(lev0.grid);
        auto numCh = lev0.grid.numChannels();
        for (size_t bi = prevNbs; bi < (size_t)newNbs; ++bi)
          for (int ch = 0; ch < numCh; ++ch)
            for (size_t ci = 0; ci < bs; ++ci)
              gridV(ch, bi, ci) = 0;
        grid.complementTopo(FWD(pol));
      }
    }
  }

  /// @brief Erode kernel — deactivate leaf voxels at the boundary of the active region
  struct AgErodeLeafKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, erodeFlags] = params;
      auto &lev = agv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(lev)::block_size;
      constexpr int dim = RM_CVREF_T(agv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (lev.valueMask[bno].isOff(cno)) return;
      auto coord = lev.table._activeKeys[bno]
                   + RM_CVREF_T(agv)::template tile_offset_to_coord<0>(cno);
      // If any face neighbor is inactive, mark for erosion
      for (int axis = 0; axis < dim; ++axis) {
        for (int dir = -1; dir <= 1; dir += 2) {
          auto neighborCoord = coord;
          neighborCoord[axis] += dir;
          auto key = RM_CVREF_T(agv)::template coord_to_key<0>(neighborCoord);
          auto nBno = lev.table.query(key);
          if (nBno == RM_CVREF_T(lev.table)::sentinel_v) {
            erodeFlags[i] = 1;
            return;
          }
          auto nOffset = RM_CVREF_T(agv)::template coord_to_tile_offset<0>(neighborCoord);
          if (lev.valueMask[nBno].isOff(nOffset)) {
            erodeFlags[i] = 1;
            return;
          }
        }
      }
    }
  };

  struct AgErodeApplyKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, erodeFlags, bg] = params;
      auto &lev = agv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(lev)::block_size;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (erodeFlags[i]) {
        lev.valueMask[bno].setOff(cno, wrapv<RM_CVREF_T(agv)::space>{});
        for (int d = 0; d < (int)lev.numChannels(); ++d) lev.grid(d, bno, cno) = bg;
      }
    }
  };

  /// @brief Erode the active topology at the leaf level by one voxel.
  /// Voxels at the boundary of the active region (with at least one inactive face neighbor)
  /// are deactivated and set to the background value.
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_erode(ExecPol &&pol, AdaptiveGridT &grid, int iterations = 1) {
    for (int iter = 0; iter < iterations; ++iter) {
      auto &lev0 = grid.level(dim_c<0>);
      auto nbs = lev0.numBlocks();
      if (nbs == 0) return;
      constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
      auto totalCells = nbs * bs;

      auto allocator = get_temporary_memory_source(pol);
      Vector<int> erodeFlags{allocator, totalCells};
      pol(range(totalCells), [flags = view<space>(erodeFlags)] ZS_LAMBDA(size_t i) mutable {
        flags[i] = 0;
      });

      auto agv = view<space>(grid);
      {
        auto params = zs::make_tuple(agv, view<space>(erodeFlags));
        pol(range(totalCells), params, AgErodeLeafKernel{});
      }
      {
        auto params = zs::make_tuple(agv, view<space>(erodeFlags), grid._background);
        pol(range(totalCells), params, AgErodeApplyKernel{});
      }
    }
  }

  // ============================================================================
  //
  //  Level Set CSG Operations (OpenVDB-style)
  //
  //  CSG Union:        result(x) = min(a(x), b(x))
  //  CSG Intersection: result(x) = max(a(x), b(x))
  //  CSG Difference:   result(x) = max(a(x), -b(x))
  //
  // ============================================================================

  /// @brief CSG combine kernel — applies a binary op to two level set grids
  /// @tparam OpTag 0 = union (min), 1 = intersection (max), 2 = difference (max(a,-b))
  template <int OpTag> struct AgCsgLeafKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[dstAgv, srcAgv, sdfChn] = params;
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(dstLev)::block_size;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (dstLev.valueMask[bno].isOff(cno)) return;

      auto coord = dstLev.table._activeKeys[bno]
                   + RM_CVREF_T(dstAgv)::template tile_offset_to_coord<0>(cno);

      auto dstVal = dstLev.grid(sdfChn, bno, cno);

      // Sample source grid at this coordinate
      using value_type = typename RM_CVREF_T(dstAgv)::value_type;
      value_type srcVal{};
      auto acc = srcAgv.getAccessor();
      bool found = acc.probeValue(sdfChn, coord, srcVal);
      if (!found) srcVal = srcAgv._background;

      value_type result{};
      if constexpr (OpTag == 0) {
        // Union: min
        result = dstVal < srcVal ? dstVal : srcVal;
      } else if constexpr (OpTag == 1) {
        // Intersection: max
        result = dstVal > srcVal ? dstVal : srcVal;
      } else {
        // Difference: max(a, -b)
        auto negSrc = -srcVal;
        result = dstVal > negSrc ? dstVal : negSrc;
      }
      dstLev.grid(sdfChn, bno, cno) = result;
    }
  };

  /// @brief Also sample source grid values into newly activated dst voxels
  template <int OpTag> struct AgCsgNewVoxelKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[dstAgv, srcAgv, sdfChn] = params;
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(dstLev)::block_size;
      auto bno = i / block_size;
      auto cno = i % block_size;
      // Process voxels that are NOT yet active in dst
      if (dstLev.valueMask[bno].isOn(cno)) return;

      auto coord = dstLev.table._activeKeys[bno]
                   + RM_CVREF_T(dstAgv)::template tile_offset_to_coord<0>(cno);

      using value_type = typename RM_CVREF_T(dstAgv)::value_type;
      value_type srcVal{};
      auto acc = srcAgv.getAccessor();
      bool found = acc.probeValue(sdfChn, coord, srcVal);
      if (!found) return;  // src also has no value here

      auto dstBg = dstAgv._background;
      value_type result{};
      if constexpr (OpTag == 0) {
        result = dstBg < srcVal ? dstBg : srcVal;
      } else if constexpr (OpTag == 1) {
        result = dstBg > srcVal ? dstBg : srcVal;
      } else {
        auto negSrc = -srcVal;
        result = dstBg > negSrc ? dstBg : negSrc;
      }
      // Only activate if result differs from background
      if (result != dstBg) {
        dstLev.grid(sdfChn, bno, cno) = result;
        dstLev.valueMask[bno].setOn(cno, wrapv<RM_CVREF_T(dstAgv)::space>{});
      }
    }
  };

  namespace detail {
    /// @brief Union source topology into destination grid at the leaf level,
    /// then apply the CSG operation to all active voxels.
    template <int OpTag, typename ExecPol, typename AdaptiveGridT,
              execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
    void ag_csg_impl(ExecPol &&pol, AdaptiveGridT &dst, const AdaptiveGridT &src,
                     size_t sdfChannel) {
      using size_type = typename AdaptiveGridT::size_type;
      constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

      // Step 1: Union source leaf topology into dst
      auto &dstLev0 = dst.level(dim_c<0>);
      const auto &srcLev0 = src.level(dim_c<0>);
      auto srcNbs = srcLev0.numBlocks();
      auto prevDstNbs = dstLev0.numBlocks();

      // Reserve space for potential new blocks
      dstLev0.resizePartition(pol, prevDstNbs + srcNbs);

      // Insert all source leaf block keys into dst table
      {
        auto srcAgv = view<space>(src);
        pol(range(srcNbs), [srcView = view<space>(srcLev0.table._activeKeys),
                            dstTab = view<space>(dstLev0.table)] ZS_LAMBDA(size_type i) mutable {
          dstTab.insert(srcView[i]);
        });
      }

      auto newDstNbs = dstLev0.numBlocks();
      if (newDstNbs > prevDstNbs) {
        dstLev0.refitToPartition();
        // Zero-initialize only the newly added blocks (not the existing ones)
        for (auto bi = prevDstNbs; bi < newDstNbs; ++bi) {
          dstLev0.valueMask[bi].setOff();
          dstLev0.childMask[bi].setOff();
        }
        auto gridV = view<space>(dstLev0.grid);
        auto numCh = dstLev0.grid.numChannels();
        for (size_t bi = prevDstNbs; bi < (size_t)newDstNbs; ++bi)
          for (int ch = 0; ch < numCh; ++ch)
            for (size_t ci = 0; ci < bs; ++ci)
              gridV(ch, bi, ci) = 0;
        dst.complementTopo(pol);
      }

      // Step 2: Apply CSG op to ALL active dst voxels
      auto dstNbs = dstLev0.numBlocks();
      {
        auto dstAgv = view<space>(dst);
        auto srcAgv = view<space>(src);
        auto params
            = zs::make_tuple(dstAgv, srcAgv, (typename AdaptiveGridT::size_type)sdfChannel);
        pol(range(dstNbs * bs), params, AgCsgLeafKernel<OpTag>{});
      }

      // Step 3: Activate new voxels from src that weren't in dst
      {
        auto dstAgv = view<space>(dst);
        auto srcAgv = view<space>(src);
        auto params
            = zs::make_tuple(dstAgv, srcAgv, (typename AdaptiveGridT::size_type)sdfChannel);
        pol(range(dstNbs * bs), params, AgCsgNewVoxelKernel<OpTag>{});
      }
    }
  }  // namespace detail

  /// @brief CSG Union of two level set grids: dst = min(dst, src)
  template <typename ExecPol, typename AdaptiveGridT>
  void ag_csg_union(ExecPol &&pol, AdaptiveGridT &dst, const AdaptiveGridT &src,
                    size_t sdfChannel = 0) {
    detail::ag_csg_impl<0>(FWD(pol), dst, src, sdfChannel);
  }

  /// @brief CSG Intersection of two level set grids: dst = max(dst, src)
  template <typename ExecPol, typename AdaptiveGridT>
  void ag_csg_intersection(ExecPol &&pol, AdaptiveGridT &dst, const AdaptiveGridT &src,
                           size_t sdfChannel = 0) {
    detail::ag_csg_impl<1>(FWD(pol), dst, src, sdfChannel);
  }

  /// @brief CSG Difference of two level set grids: dst = max(dst, -src)
  template <typename ExecPol, typename AdaptiveGridT>
  void ag_csg_difference(ExecPol &&pol, AdaptiveGridT &dst, const AdaptiveGridT &src,
                         size_t sdfChannel = 0) {
    detail::ag_csg_impl<2>(FWD(pol), dst, src, sdfChannel);
  }

  // ============================================================================
  //
  //  Flood Fill (OpenVDB-style)
  //
  //  Propagate sign information from narrow band outward through the tree.
  //  Interior voxels (fully enclosed by the surface) get negative background,
  //  exterior voxels get positive background.
  //
  // ============================================================================

  /// @brief Leaf-level sign propagation: mark voxels whose neighbors have
  /// definite sign from the narrow band
  struct AgFloodFillSignKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, sdfChn, signs, changed] = params;
      auto &lev = agv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(lev)::block_size;
      constexpr int dim = RM_CVREF_T(agv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (lev.valueMask[bno].isOff(cno)) return;
      if (signs[i] != 0) return;  // already determined

      auto coord = lev.table._activeKeys[bno]
                   + RM_CVREF_T(agv)::template tile_offset_to_coord<0>(cno);

      for (int axis = 0; axis < dim; ++axis) {
        for (int dir = -1; dir <= 1; dir += 2) {
          auto neighborCoord = coord;
          neighborCoord[axis] += dir;
          auto key = RM_CVREF_T(agv)::template coord_to_key<0>(neighborCoord);
          auto nBno = lev.table.query(key);
          if (nBno == RM_CVREF_T(lev.table)::sentinel_v) continue;
          auto nOffset = RM_CVREF_T(agv)::template coord_to_tile_offset<0>(neighborCoord);
          if (lev.valueMask[nBno].isOff(nOffset)) continue;

          auto nIdx = (size_t)nBno * block_size + nOffset;
          if (signs[nIdx] != 0) {
            signs[i] = signs[nIdx];
            atomic_add(wrapv<RM_CVREF_T(agv)::space>{}, changed, 1);
            return;
          }
        }
      }
    }
  };

  /// @brief Flood fill a level set SDF grid.
  /// Voxels in the narrow band keep their computed SDF values.
  /// Voxels outside the narrow band receive +/- background based on sign propagation.
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_flood_fill(ExecPol &&pol, AdaptiveGridT &grid, size_t sdfChannel = 0) {
    using value_type = typename AdaptiveGridT::value_type;
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    auto totalCells = nbs * bs;
    auto background = grid._background;

    auto allocator = get_temporary_memory_source(pol);
    // +1 = exterior, -1 = interior, 0 = unknown
    Vector<int> signs{allocator, totalCells};
    Vector<int> changed{allocator, 1};

    auto agv = view<space>(grid);

    // Initialize signs: assign +1/-1 to voxels in the narrow band
    pol(range(totalCells),
        [agv, sdfChn = (size_type)sdfChannel, signsPtr = view<space>(signs),
         bg = background] ZS_LAMBDA(size_t i) mutable {
          auto &lev = agv.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(lev)::block_size;
          auto bno = i / bsz;
          auto cno = i % bsz;
          if (lev.valueMask[bno].isOn(cno)) {
            auto val = lev.grid(sdfChn, bno, cno);
            // In narrow band if |val| < |background|
            auto absVal = val < 0 ? -val : val;
            auto absBg = bg < 0 ? -bg : bg;
            if (absVal < absBg) {
              signsPtr[i] = val < 0 ? -1 : 1;
            } else {
              signsPtr[i] = 0;
            }
          } else {
            signsPtr[i] = 0;
          }
        });

    // Iteratively propagate signs
    constexpr int maxIters = 256;
    for (int iter = 0; iter < maxIters; ++iter) {
      pol(range(1),
          [ch = view<space>(changed)] ZS_LAMBDA(size_t) mutable { ch[0] = 0; });

      auto params = zs::make_tuple(agv, (size_type)sdfChannel, view<space>(signs),
                                   view<space>(changed).data());
      pol(range(totalCells), params, AgFloodFillSignKernel{});

      // Check convergence
      if (changed.getVal(0) == 0) break;
    }

    // Apply flood fill: set sign-determined voxels to +/- background
    pol(range(totalCells),
        [agv, sdfChn = (size_type)sdfChannel, signsPtr = view<space>(signs),
         bg = background] ZS_LAMBDA(size_t i) mutable {
          auto &lev = agv.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(lev)::block_size;
          auto bno = i / bsz;
          auto cno = i % bsz;
          if (lev.valueMask[bno].isOff(cno)) return;
          auto val = lev.grid(sdfChn, bno, cno);
          auto absVal = val < 0 ? -val : val;
          auto absBg = bg < 0 ? -bg : bg;
          // Only modify voxels outside the narrow band
          if (absVal >= absBg && signsPtr[i] != 0) {
            lev.grid(sdfChn, bno, cno) = signsPtr[i] < 0 ? -absBg : absBg;
          }
        });
  }

  // ============================================================================
  //
  //  Differential Operators (OpenVDB-style)
  //
  //  Gradient, Laplacian, Mean Curvature — computed on the leaf level
  //  using central finite differences in index space.
  //
  // ============================================================================

  /// @brief Compute gradient of a scalar field at each active leaf voxel.
  /// Uses central differences: grad_d = (f(x+e_d) - f(x-e_d)) / (2*dx)
  /// @param srcChannel Source scalar channel
  /// @param dstChannel First destination channel (dim channels written: dstChannel..dstChannel+dim-1)
  struct AgGradientKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, dstAgv, srcChn, dstChn, invTwoDx] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;

      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);

      auto acc = srcAgv.getAccessor();
      using value_type = typename RM_CVREF_T(srcAgv)::value_type;

      for (int d = 0; d < dim; ++d) {
        auto coordP = coord;
        auto coordM = coord;
        coordP[d] += 1;
        coordM[d] -= 1;
        value_type vp{}, vm{};
        acc.probeValue(srcChn, coordP, vp);
        acc.probeValue(srcChn, coordM, vm);
        dstLev.grid(dstChn + d, bno, cno) = (vp - vm) * invTwoDx;
      }
    }
  };

  /// @brief Compute gradient of a scalar channel, writing dim channels of output.
  /// @param src Source adaptive grid (read-only)
  /// @param dst Destination adaptive grid (must have same topology, with enough channels)
  /// @param srcChannel Source scalar channel index
  /// @param dstChannel First destination channel index (writes dstChannel to dstChannel+dim-1)
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_gradient(ExecPol &&pol, const AdaptiveGridT &src, AdaptiveGridT &dst, size_t srcChannel,
                   size_t dstChannel) {
    using size_type = typename AdaptiveGridT::size_type;
    using value_type = typename AdaptiveGridT::value_type;
    auto &srcLev0 = src.level(dim_c<0>);
    auto nbs = srcLev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

    auto dx = src.voxelSize()[0];
    value_type invTwoDx = (value_type)1 / (dx + dx);

    auto srcAgv = view<space>(src);
    auto dstAgv = view<space>(dst);
    auto params = zs::make_tuple(srcAgv, dstAgv, (size_type)srcChannel, (size_type)dstChannel,
                                 invTwoDx);
    pol(range(nbs * bs), params, AgGradientKernel{});
  }

  /// @brief Compute Laplacian of a scalar field at each active leaf voxel.
  /// Uses standard 2nd-order central stencil:
  ///   lap(f) = sum_d (f(x+e_d) + f(x-e_d) - 2*f(x)) / dx^2
  struct AgLaplacianKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, dstAgv, srcChn, dstChn, invDx2] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;

      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
      auto acc = srcAgv.getAccessor();
      using value_type = typename RM_CVREF_T(srcAgv)::value_type;

      auto center = srcLev.grid(srcChn, bno, cno);
      value_type lap = (value_type)0;
      for (int d = 0; d < dim; ++d) {
        auto coordP = coord;
        auto coordM = coord;
        coordP[d] += 1;
        coordM[d] -= 1;
        value_type vp{}, vm{};
        acc.probeValue(srcChn, coordP, vp);
        acc.probeValue(srcChn, coordM, vm);
        lap += (vp + vm - (value_type)2 * center);
      }
      dstLev.grid(dstChn, bno, cno) = lap * invDx2;
    }
  };

  /// @brief Compute the Laplacian of a scalar channel.
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_laplacian(ExecPol &&pol, const AdaptiveGridT &src, AdaptiveGridT &dst, size_t srcChannel,
                    size_t dstChannel) {
    using size_type = typename AdaptiveGridT::size_type;
    using value_type = typename AdaptiveGridT::value_type;
    auto &srcLev0 = src.level(dim_c<0>);
    auto nbs = srcLev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

    auto dx = src.voxelSize()[0];
    value_type invDx2 = (value_type)1 / (dx * dx);

    auto srcAgv = view<space>(src);
    auto dstAgv = view<space>(dst);
    auto params = zs::make_tuple(srcAgv, dstAgv, (size_type)srcChannel, (size_type)dstChannel,
                                 invDx2);
    pol(range(nbs * bs), params, AgLaplacianKernel{});
  }

  /// @brief Compute mean curvature of a level set SDF.
  ///   kappa = div(grad(phi) / |grad(phi)|)
  /// Approximated via:
  ///   kappa = (lap(phi) - <n, H(phi)*n>) / |grad(phi)|
  /// where n = grad(phi)/|grad(phi)|, H = Hessian.
  /// Here we use a simpler direct formula from finite differences.
  struct AgMeanCurvatureKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, dstAgv, srcChn, dstChn, invDx, invDx2] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;

      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
      auto acc = srcAgv.getAccessor();
      using value_type = typename RM_CVREF_T(srcAgv)::value_type;

      auto center = srcLev.grid(srcChn, bno, cno);

      // First derivatives (central)
      value_type Dx[dim]{};
      value_type vp[dim]{}, vm[dim]{};
      for (int d = 0; d < dim; ++d) {
        auto cP = coord, cM = coord;
        cP[d] += 1;
        cM[d] -= 1;
        acc.probeValue(srcChn, cP, vp[d]);
        acc.probeValue(srcChn, cM, vm[d]);
        Dx[d] = (vp[d] - vm[d]) * invDx * (value_type)0.5;
      }

      // Gradient magnitude squared
      value_type gradMag2 = (value_type)0;
      for (int d = 0; d < dim; ++d) gradMag2 += Dx[d] * Dx[d];

      if (gradMag2 < (value_type)1e-20) {
        dstLev.grid(dstChn, bno, cno) = (value_type)0;
        return;
      }

      // Laplacian
      value_type lap = (value_type)0;
      for (int d = 0; d < dim; ++d) lap += (vp[d] + vm[d] - (value_type)2 * center) * invDx2;

      // Second cross derivatives for Hessian dot product with normal
      // kappa = (|grad|^2 * lap - sum_ij Dx[i]*Dx[j]*D2[i][j]) / |grad|^3
      if constexpr (dim == 3) {
        value_type D2xx = (vp[0] + vm[0] - (value_type)2 * center) * invDx2;
        value_type D2yy = (vp[1] + vm[1] - (value_type)2 * center) * invDx2;
        value_type D2zz = (vp[2] + vm[2] - (value_type)2 * center) * invDx2;

        // Cross derivatives: D2xy = (f(x+1,y+1) - f(x+1,y-1) - f(x-1,y+1) + f(x-1,y-1)) / (4*dx^2)
        auto sampleCross = [&](int a, int b) -> value_type {
          auto c1 = coord, c2 = coord, c3 = coord, c4 = coord;
          c1[a] += 1;
          c1[b] += 1;
          c2[a] += 1;
          c2[b] -= 1;
          c3[a] -= 1;
          c3[b] += 1;
          c4[a] -= 1;
          c4[b] -= 1;
          value_type v1{}, v2{}, v3{}, v4{};
          acc.probeValue(srcChn, c1, v1);
          acc.probeValue(srcChn, c2, v2);
          acc.probeValue(srcChn, c3, v3);
          acc.probeValue(srcChn, c4, v4);
          return (v1 - v2 - v3 + v4) * invDx2 * (value_type)0.25;
        };
        value_type D2xy = sampleCross(0, 1);
        value_type D2xz = sampleCross(0, 2);
        value_type D2yz = sampleCross(1, 2);

        value_type nHn = Dx[0] * Dx[0] * D2xx + Dx[1] * Dx[1] * D2yy + Dx[2] * Dx[2] * D2zz
                         + (value_type)2
                               * (Dx[0] * Dx[1] * D2xy + Dx[0] * Dx[2] * D2xz
                                  + Dx[1] * Dx[2] * D2yz);

        value_type gradMag = zs::sqrt(gradMag2);
        value_type gradMag3 = gradMag2 * gradMag;
        dstLev.grid(dstChn, bno, cno) = (gradMag2 * lap - nHn) / gradMag3;
      } else if constexpr (dim == 2) {
        value_type D2xx = (vp[0] + vm[0] - (value_type)2 * center) * invDx2;
        value_type D2yy = (vp[1] + vm[1] - (value_type)2 * center) * invDx2;

        auto c1 = coord, c2 = coord, c3 = coord, c4 = coord;
        c1[0] += 1;
        c1[1] += 1;
        c2[0] += 1;
        c2[1] -= 1;
        c3[0] -= 1;
        c3[1] += 1;
        c4[0] -= 1;
        c4[1] -= 1;
        value_type v1{}, v2{}, v3{}, v4{};
        acc.probeValue(srcChn, c1, v1);
        acc.probeValue(srcChn, c2, v2);
        acc.probeValue(srcChn, c3, v3);
        acc.probeValue(srcChn, c4, v4);
        value_type D2xy = (v1 - v2 - v3 + v4) * invDx2 * (value_type)0.25;

        value_type nHn
            = Dx[0] * Dx[0] * D2xx + Dx[1] * Dx[1] * D2yy + (value_type)2 * Dx[0] * Dx[1] * D2xy;

        value_type gradMag = zs::sqrt(gradMag2);
        value_type gradMag3 = gradMag2 * gradMag;
        dstLev.grid(dstChn, bno, cno) = (gradMag2 * lap - nHn) / gradMag3;
      }
    }
  };

  /// @brief Compute mean curvature of a level set SDF field.
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_mean_curvature(ExecPol &&pol, const AdaptiveGridT &src, AdaptiveGridT &dst,
                         size_t srcChannel, size_t dstChannel) {
    using size_type = typename AdaptiveGridT::size_type;
    using value_type = typename AdaptiveGridT::value_type;
    auto &srcLev0 = src.level(dim_c<0>);
    auto nbs = srcLev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

    auto dx = src.voxelSize()[0];
    value_type invDx = (value_type)1 / dx;
    value_type invDx2 = invDx * invDx;

    auto srcAgv = view<space>(src);
    auto dstAgv = view<space>(dst);
    auto params = zs::make_tuple(srcAgv, dstAgv, (size_type)srcChannel, (size_type)dstChannel,
                                 invDx, invDx2);
    pol(range(nbs * bs), params, AgMeanCurvatureKernel{});
  }

  // ============================================================================
  //
  //  Upwind / WENO5 Gradient (OpenVDB-style HJ schemes)
  //
  //  Central differences are inappropriate for Hamilton-Jacobi equations
  //  (advection, reinitialization) because they don't respect characteristic
  //  direction and generate oscillations near discontinuities.
  //
  //  - ag_gradient_upwind: 1st-order Godunov upwind gradient magnitude
  //  - ag_reinitialize_weno5: 5th-order HJ-WENO5 + Godunov reinitialization
  //
  // ============================================================================

  /// @brief First-order Godunov upwind gradient magnitude kernel.
  /// For each active voxel, computes |grad(phi)| using the Godunov upwind scheme:
  ///   If S(phi) > 0: |grad|^2 = sum_d max(max(D_d^-, 0)^2, min(D_d^+, 0)^2)
  ///   If S(phi) < 0: |grad|^2 = sum_d max(min(D_d^-, 0)^2, max(D_d^+, 0)^2)
  struct AgGradientUpwindKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, dstAgv, srcChn, dstChn, invDx] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;

      using value_type = typename RM_CVREF_T(srcAgv)::value_type;
      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
      auto acc = srcAgv.getAccessor();
      auto phi = srcLev.grid(srcChn, bno, cno);

      value_type gradMag2 = (value_type)0;
      for (int d = 0; d < dim; ++d) {
        auto cP = coord, cM = coord;
        cP[d] += 1;
        cM[d] -= 1;
        value_type vp{}, vm{};
        acc.probeValue(srcChn, cP, vp);
        acc.probeValue(srcChn, cM, vm);

        value_type Dm = (phi - vm) * invDx;  // backward difference
        value_type Dp = (vp - phi) * invDx;  // forward difference

        if (phi > (value_type)0) {
          value_type a = Dm > (value_type)0 ? Dm : (value_type)0;
          value_type b = Dp < (value_type)0 ? -Dp : (value_type)0;
          gradMag2 += a > b ? a * a : b * b;
        } else {
          value_type a = Dm < (value_type)0 ? -Dm : (value_type)0;
          value_type b = Dp > (value_type)0 ? Dp : (value_type)0;
          gradMag2 += a > b ? a * a : b * b;
        }
      }
      dstLev.grid(dstChn, bno, cno) = zs::sqrt(gradMag2);
    }
  };

  /// @brief Compute upwind gradient magnitude of a scalar field (for HJ equations).
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_gradient_upwind(ExecPol &&pol, const AdaptiveGridT &src, AdaptiveGridT &dst,
                          size_t srcChannel, size_t dstChannel) {
    using size_type = typename AdaptiveGridT::size_type;
    using value_type = typename AdaptiveGridT::value_type;
    auto &srcLev0 = src.level(dim_c<0>);
    auto nbs = srcLev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

    auto dx = src.voxelSize()[0];
    value_type invDx = (value_type)1 / dx;

    auto srcAgv = view<space>(src);
    auto dstAgv = view<space>(dst);
    auto params = zs::make_tuple(srcAgv, dstAgv, (size_type)srcChannel, (size_type)dstChannel,
                                 invDx);
    pol(range(nbs * bs), params, AgGradientUpwindKernel{});
  }

  /// @brief 5th-order HJ-WENO reinitialization kernel.
  /// Uses WENO5 reconstruction for left-biased (D⁻) and right-biased (D⁺)
  /// derivatives combined via Godunov's scheme for the Eikonal equation.
  /// This is the standard scheme used by OpenVDB for level set reinitialization.
  struct AgReinitializeWeno5Kernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, dstAgv, sdfChn, dt, invDx] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;

      using value_type = typename RM_CVREF_T(srcAgv)::value_type;
      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
      auto acc = srcAgv.getAccessor();
      auto phi = srcLev.grid(sdfChn, bno, cno);

      // WENO5 epsilon for non-linear weights
      constexpr value_type eps = (value_type)1e-6;

      value_type gradMag2 = (value_type)0;

      for (int d = 0; d < dim; ++d) {
        // Sample 7-point stencil: φ_{i-3} ... φ_{i+3}
        value_type v[7];
        for (int s = -3; s <= 3; ++s) {
          auto c = coord;
          c[d] += s;
          if (s == 0)
            v[s + 3] = phi;
          else
            acc.probeValue(sdfChn, c, v[s + 3]);
        }
        // v[0]=φ_{i-3}, v[1]=φ_{i-2}, v[2]=φ_{i-1}, v[3]=φ_i, v[4]=φ_{i+1}, v[5]=φ_{i+2}, v[6]=φ_{i+3}

        // First-order divided differences
        value_type u1 = (v[1] - v[0]) * invDx;  // (φ_{i-2} - φ_{i-3})/dx
        value_type u2 = (v[2] - v[1]) * invDx;  // (φ_{i-1} - φ_{i-2})/dx
        value_type u3 = (v[3] - v[2]) * invDx;  // (φ_i     - φ_{i-1})/dx
        value_type u4 = (v[4] - v[3]) * invDx;  // (φ_{i+1} - φ_i    )/dx
        value_type u5 = (v[5] - v[4]) * invDx;  // (φ_{i+2} - φ_{i+1})/dx
        value_type u6 = (v[6] - v[5]) * invDx;  // (φ_{i+3} - φ_{i+2})/dx

        // === Left-biased (D⁻) WENO5 reconstruction ===
        value_type p1m = u1 / (value_type)3 - (value_type)7 * u2 / (value_type)6
                         + (value_type)11 * u3 / (value_type)6;
        value_type p2m = -u2 / (value_type)6 + (value_type)5 * u3 / (value_type)6
                         + u4 / (value_type)3;
        value_type p3m = u3 / (value_type)3 + (value_type)5 * u4 / (value_type)6
                         - u5 / (value_type)6;

        // Smoothness indicators (left)
        value_type b1m = (value_type)(13.0 / 12.0)
                             * (u1 - (value_type)2 * u2 + u3) * (u1 - (value_type)2 * u2 + u3)
                         + (value_type)0.25
                               * (u1 - (value_type)4 * u2 + (value_type)3 * u3)
                               * (u1 - (value_type)4 * u2 + (value_type)3 * u3);
        value_type b2m = (value_type)(13.0 / 12.0)
                             * (u2 - (value_type)2 * u3 + u4) * (u2 - (value_type)2 * u3 + u4)
                         + (value_type)0.25 * (u2 - u4) * (u2 - u4);
        value_type b3m = (value_type)(13.0 / 12.0)
                             * (u3 - (value_type)2 * u4 + u5) * (u3 - (value_type)2 * u4 + u5)
                         + (value_type)0.25
                               * ((value_type)3 * u3 - (value_type)4 * u4 + u5)
                               * ((value_type)3 * u3 - (value_type)4 * u4 + u5);

        // Weights (left)
        value_type a1m = (value_type)0.1 / ((eps + b1m) * (eps + b1m));
        value_type a2m = (value_type)0.6 / ((eps + b2m) * (eps + b2m));
        value_type a3m = (value_type)0.3 / ((eps + b3m) * (eps + b3m));
        value_type sumAm = a1m + a2m + a3m;

        value_type Dminus = (a1m * p1m + a2m * p2m + a3m * p3m) / sumAm;

        // === Right-biased (D⁺) WENO5 reconstruction ===
        // Mirror the stencil: use u6,u5,u4,u3,u2
        value_type p1p = u6 / (value_type)3 - (value_type)7 * u5 / (value_type)6
                         + (value_type)11 * u4 / (value_type)6;
        value_type p2p = -u5 / (value_type)6 + (value_type)5 * u4 / (value_type)6
                         + u3 / (value_type)3;
        value_type p3p = u4 / (value_type)3 + (value_type)5 * u3 / (value_type)6
                         - u2 / (value_type)6;

        // Smoothness indicators (right — same formula, mirrored inputs)
        value_type b1p = (value_type)(13.0 / 12.0)
                             * (u6 - (value_type)2 * u5 + u4) * (u6 - (value_type)2 * u5 + u4)
                         + (value_type)0.25
                               * (u6 - (value_type)4 * u5 + (value_type)3 * u4)
                               * (u6 - (value_type)4 * u5 + (value_type)3 * u4);
        value_type b2p = (value_type)(13.0 / 12.0)
                             * (u5 - (value_type)2 * u4 + u3) * (u5 - (value_type)2 * u4 + u3)
                         + (value_type)0.25 * (u5 - u3) * (u5 - u3);
        value_type b3p = (value_type)(13.0 / 12.0)
                             * (u4 - (value_type)2 * u3 + u2) * (u4 - (value_type)2 * u3 + u2)
                         + (value_type)0.25
                               * ((value_type)3 * u4 - (value_type)4 * u3 + u2)
                               * ((value_type)3 * u4 - (value_type)4 * u3 + u2);

        // Weights (right)
        value_type a1p = (value_type)0.1 / ((eps + b1p) * (eps + b1p));
        value_type a2p = (value_type)0.6 / ((eps + b2p) * (eps + b2p));
        value_type a3p = (value_type)0.3 / ((eps + b3p) * (eps + b3p));
        value_type sumAp = a1p + a2p + a3p;

        value_type Dplus = (a1p * p1p + a2p * p2p + a3p * p3p) / sumAp;

        // === Godunov upwind combination ===
        if (phi > (value_type)0) {
          value_type a = Dminus > (value_type)0 ? Dminus : (value_type)0;
          value_type b = Dplus < (value_type)0 ? -Dplus : (value_type)0;
          gradMag2 += a > b ? a * a : b * b;
        } else {
          value_type a = Dminus < (value_type)0 ? -Dminus : (value_type)0;
          value_type b = Dplus > (value_type)0 ? Dplus : (value_type)0;
          gradMag2 += a > b ? a * a : b * b;
        }
      }

      value_type gradMag = zs::sqrt(gradMag2);

      // Smoothed sign function
      value_type S = phi / zs::sqrt(phi * phi + (value_type)1);

      dstLev.grid(sdfChn, bno, cno) = phi - dt * S * (gradMag - (value_type)1);
    }
  };

  /// @brief Reinitialize a level set SDF using 5th-order HJ-WENO scheme.
  /// This provides significantly better accuracy than the 1st-order Godunov
  /// scheme in ag_reinitialize, at the cost of a wider stencil (3 cells each side).
  /// Matches OpenVDB's reinitialization quality.
  /// @param grid The level set grid
  /// @param sdfChannel The SDF channel
  /// @param iterations Number of reinitialization iterations
  /// @param dt CFL-stable time step (default: 0.5 * dx)
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_reinitialize_weno5(ExecPol &&pol, AdaptiveGridT &grid, size_t sdfChannel = 0,
                             int iterations = 5,
                             typename AdaptiveGridT::value_type dt = -1) {
    using value_type = typename AdaptiveGridT::value_type;
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

    auto dx = grid.voxelSize()[0];
    if (dt < (value_type)0) dt = (value_type)0.5 * dx;
    value_type invDx = (value_type)1 / dx;

    auto tmpGrid = grid.clone(grid.memoryLocation());

    for (int iter = 0; iter < iterations; ++iter) {
      auto &srcGrid = (iter % 2 == 0) ? grid : tmpGrid;
      auto &dstGrid = (iter % 2 == 0) ? tmpGrid : grid;
      auto srcAgv = view<space>(srcGrid);
      auto dstAgv = view<space>(dstGrid);
      auto params = zs::make_tuple(srcAgv, dstAgv, (size_type)sdfChannel, dt, invDx);
      pol(range(nbs * bs), params, AgReinitializeWeno5Kernel{});
    }

    if (iterations % 2 == 1) {
      auto srcAgv = view<space>(tmpGrid);
      auto dstAgv = view<space>(grid);
      pol(range(nbs * bs),
          [srcAgv, dstAgv, chn = (size_type)sdfChannel] ZS_LAMBDA(size_t i) mutable {
            auto &srcLev = srcAgv.level(dim_c<0>);
            auto &dstLev = dstAgv.level(dim_c<0>);
            constexpr auto bsz = RM_CVREF_T(srcLev)::block_size;
            auto bno = i / bsz;
            auto cno = i % bsz;
            if (srcLev.valueMask[bno].isOn(cno)) {
              dstLev.grid(chn, bno, cno) = srcLev.grid(chn, bno, cno);
            }
          });
    }
  }

  // ============================================================================
  //
  //  Filtering Operations
  //
  // ============================================================================

  /// @brief Gaussian filter (one iteration of discrete Laplacian smoothing).
  ///   f_new(x) = (1 - 6*alpha) * f(x) + alpha * sum_neighbors f(n)
  /// where alpha = dt / dx^2 for a diffusion step.
  struct AgGaussianSmoothKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, dstAgv, chn, alpha] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;

      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
      auto acc = srcAgv.getAccessor();
      using value_type = typename RM_CVREF_T(srcAgv)::value_type;

      auto center = srcLev.grid(chn, bno, cno);
      value_type neighborSum = (value_type)0;
      for (int d = 0; d < dim; ++d) {
        auto cP = coord, cM = coord;
        cP[d] += 1;
        cM[d] -= 1;
        value_type vp{}, vm{};
        acc.probeValue(chn, cP, vp);
        acc.probeValue(chn, cM, vm);
        neighborSum += vp + vm;
      }
      dstLev.grid(chn, bno, cno)
          = ((value_type)1 - (value_type)(2 * dim) * alpha) * center + alpha * neighborSum;
    }
  };

  /// @brief Apply Gaussian (Laplacian diffusion) smoothing to a scalar channel.
  /// @param channel The channel to smooth
  /// @param alpha Diffusion coefficient (0 < alpha < 1/(2*dim) for stability)
  /// @param iterations Number of smoothing iterations
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_smooth(ExecPol &&pol, AdaptiveGridT &grid, size_t channel,
                 typename AdaptiveGridT::value_type alpha = 0.1, int iterations = 1) {
    using value_type = typename AdaptiveGridT::value_type;
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

    // Clamp alpha for stability
    constexpr int dim = AdaptiveGridT::dim;
    value_type maxAlpha = (value_type)1 / ((value_type)(2 * dim));
    if (alpha > maxAlpha) alpha = maxAlpha;

    // Double-buffer: clone grid for ping-pong
    auto tmpGrid = grid.clone(grid.memoryLocation());

    for (int iter = 0; iter < iterations; ++iter) {
      auto &srcGrid = (iter % 2 == 0) ? grid : tmpGrid;
      auto &dstGrid = (iter % 2 == 0) ? tmpGrid : grid;
      auto srcAgv = view<space>(srcGrid);
      auto dstAgv = view<space>(dstGrid);
      auto params = zs::make_tuple(srcAgv, dstAgv, (size_type)channel, alpha);
      pol(range(nbs * bs), params, AgGaussianSmoothKernel{});
    }

    // If odd number of iterations, result is in tmpGrid; copy back
    if (iterations % 2 == 1) {
      auto srcAgv = view<space>(tmpGrid);
      auto dstAgv = view<space>(grid);
      pol(range(nbs * bs),
          [srcAgv, dstAgv, chn = (size_type)channel] ZS_LAMBDA(size_t i) mutable {
            auto &srcLev = srcAgv.level(dim_c<0>);
            auto &dstLev = dstAgv.level(dim_c<0>);
            constexpr auto bsz = RM_CVREF_T(srcLev)::block_size;
            auto bno = i / bsz;
            auto cno = i % bsz;
            if (srcLev.valueMask[bno].isOn(cno)) {
              dstLev.grid(chn, bno, cno) = srcLev.grid(chn, bno, cno);
            }
          });
    }
  }

  // ============================================================================
  //
  //  Advection (OpenVDB-style Semi-Lagrangian)
  //
  //  For each active voxel at position x, trace back along the velocity field
  //  by -dt to find the departure point, then sample the scalar field there.
  //
  // ============================================================================

  /// @brief Semi-Lagrangian advection kernel
  struct AgAdvectSemiLagrangianKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, velAgv, dstAgv, sdfChn, velChn, dt] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;

      using value_type = typename RM_CVREF_T(srcAgv)::value_type;
      using coord_type = typename RM_CVREF_T(srcAgv)::coord_type;

      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
      auto coordF = coord.template cast<value_type>();

      // Sample velocity at current position (in index space)
      coord_type vel{};
      for (int d = 0; d < dim; ++d)
        vel[d] = velAgv.iSample(velChn + d, coordF);

      // Trace back to departure point
      auto departure = coordF - vel * dt;

      // Sample scalar field at departure point
      auto val = srcAgv.iSample(sdfChn, departure);
      dstLev.grid(sdfChn, bno, cno) = val;
    }
  };

  /// @brief Advect a scalar field by a velocity field using semi-Lagrangian method.
  /// @param field The scalar field to advect (modified in-place)
  /// @param velocity The velocity field (in index space coordinates)
  /// @param dt Time step
  /// @param sdfChannel Scalar field channel
  /// @param velChannel First velocity channel (reads velChannel..velChannel+dim-1)
  template <typename ExecPol, typename ScalarGridT, typename VelGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_advect(ExecPol &&pol, ScalarGridT &field, const VelGridT &velocity,
                 typename ScalarGridT::value_type dt, size_t sdfChannel = 0,
                 size_t velChannel = 0) {
    using size_type = typename ScalarGridT::size_type;
    auto &lev0 = field.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = ScalarGridT::template get_tile_size<0>();

    auto tmpGrid = field.clone(field.memoryLocation());
    auto srcAgv = view<space>(field);
    auto velAgv = view<space>(velocity);
    auto dstAgv = view<space>(tmpGrid);

    auto params = zs::make_tuple(srcAgv, velAgv, dstAgv, (size_type)sdfChannel,
                                 (size_type)velChannel, dt);
    pol(range(nbs * bs), params, AgAdvectSemiLagrangianKernel{});

    // Copy result back
    pol(range(nbs * bs),
        [srcAgv2 = view<space>(tmpGrid), dstAgv2 = view<space>(field),
         chn = (size_type)sdfChannel] ZS_LAMBDA(size_t i) mutable {
          auto &srcLev = srcAgv2.level(dim_c<0>);
          auto &dstLev = dstAgv2.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(srcLev)::block_size;
          auto bno = i / bsz;
          auto cno = i % bsz;
          if (srcLev.valueMask[bno].isOn(cno)) {
            dstLev.grid(chn, bno, cno) = srcLev.grid(chn, bno, cno);
          }
        });
  }

  // ============================================================================
  //
  //  MacCormack Advection (OpenVDB-style 2nd-order)
  //
  //  Reduces numerical diffusion compared to 1st-order semi-Lagrangian by
  //  applying a predictor-corrector step with clamping to prevent oscillation.
  //
  //  Algorithm:
  //    1. Forward SL:  φ̂  = SL(φ,  v, -dt)   (predict)
  //    2. Backward SL: φ̃  = SL(φ̂, v, +dt)   (back-trace to check error)
  //    3. Correction:  φ* = φ̂ + 0.5*(φ - φ̃)  (compensate for error)
  //    4. Clamp φ* to extrema of the departure stencil to prevent overshoot
  //
  // ============================================================================

  /// @brief MacCormack semi-Lagrangian advection kernel — predictor step
  struct AgAdvectMacCormackForwardKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, velAgv, dstAgv, sdfChn, velChn, dt] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;

      using value_type = typename RM_CVREF_T(srcAgv)::value_type;
      using coord_type = typename RM_CVREF_T(srcAgv)::coord_type;

      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
      auto coordF = coord.template cast<value_type>();

      // Sample velocity at current position
      coord_type vel{};
      for (int d = 0; d < dim; ++d)
        vel[d] = velAgv.iSample(velChn + d, coordF);

      // Trace back to departure point
      auto departure = coordF - vel * dt;

      // Semi-Lagrangian sample at departure point
      dstLev.grid(sdfChn, bno, cno) = srcAgv.iSample(sdfChn, departure);
    }
  };

  /// @brief MacCormack correction kernel — apply error compensation with clamping
  struct AgAdvectMacCormackCorrectionKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[origAgv, fwdAgv, bwdAgv, velAgv, dstAgv, sdfChn, velChn, dt] = params;
      auto &origLev = origAgv.level(dim_c<0>);
      auto &fwdLev = fwdAgv.level(dim_c<0>);
      auto &bwdLev = bwdAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(origLev)::block_size;
      constexpr int dim = RM_CVREF_T(origAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (origLev.valueMask[bno].isOff(cno)) return;

      using value_type = typename RM_CVREF_T(origAgv)::value_type;
      using coord_type = typename RM_CVREF_T(origAgv)::coord_type;

      auto coord = origLev.table._activeKeys[bno]
                   + RM_CVREF_T(origAgv)::template tile_offset_to_coord<0>(cno);
      auto coordF = coord.template cast<value_type>();

      auto phiOrig = origLev.grid(sdfChn, bno, cno);
      auto phiFwd = fwdLev.grid(sdfChn, bno, cno);
      auto phiBwd = bwdLev.grid(sdfChn, bno, cno);

      // MacCormack correction
      value_type phiCorr = phiFwd + (value_type)0.5 * (phiOrig - phiBwd);

      // Clamp to extrema of the departure stencil to prevent oscillation
      // Find the departure point
      coord_type vel{};
      for (int d = 0; d < dim; ++d)
        vel[d] = velAgv.iSample(velChn + d, coordF);
      auto departure = coordF - vel * dt;

      // Find min/max in 2^dim neighborhood around departure point
      auto acc = origAgv.getAccessor();
      auto dFloor = departure;
      for (int d = 0; d < dim; ++d)
        dFloor[d] = zs::floor(departure[d]);

      value_type vMin = phiFwd, vMax = phiFwd;  // initialize with forward result
      for (int iz = 0; iz < (dim > 2 ? 2 : 1); ++iz)
        for (int iy = 0; iy < (dim > 1 ? 2 : 1); ++iy)
          for (int ix = 0; ix < 2; ++ix) {
            auto sampleCoord = dFloor;
            sampleCoord[0] += (value_type)ix;
            if constexpr (dim > 1) sampleCoord[1] += (value_type)iy;
            if constexpr (dim > 2) sampleCoord[2] += (value_type)iz;
            auto sc = sampleCoord.template cast<typename RM_CVREF_T(origAgv)::integer_coord_component_type>();
            value_type sv{};
            acc.probeValue(sdfChn, sc, sv);
            if (sv < vMin) vMin = sv;
            if (sv > vMax) vMax = sv;
          }

      // Clamp corrected value
      if (phiCorr < vMin) phiCorr = vMin;
      if (phiCorr > vMax) phiCorr = vMax;

      dstLev.grid(sdfChn, bno, cno) = phiCorr;
    }
  };

  /// @brief Advect a scalar field using MacCormack semi-Lagrangian method.
  /// Provides 2nd-order temporal accuracy with clamping to prevent oscillation.
  /// This matches OpenVDB's advection quality.
  /// @param field The scalar field to advect (modified in-place)
  /// @param velocity The velocity field
  /// @param dt Time step
  /// @param sdfChannel Scalar field channel
  /// @param velChannel First velocity channel
  template <typename ExecPol, typename ScalarGridT, typename VelGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_advect_maccormack(ExecPol &&pol, ScalarGridT &field, const VelGridT &velocity,
                            typename ScalarGridT::value_type dt, size_t sdfChannel = 0,
                            size_t velChannel = 0) {
    using value_type = typename ScalarGridT::value_type;
    using size_type = typename ScalarGridT::size_type;
    auto &lev0 = field.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = ScalarGridT::template get_tile_size<0>();

    // Step 1: Forward semi-Lagrangian (φ → φ̂)
    auto fwdGrid = field.clone(field.memoryLocation());
    {
      auto srcAgv = view<space>(field);
      auto velAgv = view<space>(velocity);
      auto dstAgv = view<space>(fwdGrid);
      auto params = zs::make_tuple(srcAgv, velAgv, dstAgv, (size_type)sdfChannel,
                                   (size_type)velChannel, dt);
      pol(range(nbs * bs), params, AgAdvectMacCormackForwardKernel{});
    }

    // Step 2: Backward semi-Lagrangian (φ̂ → φ̃)
    auto bwdGrid = field.clone(field.memoryLocation());
    {
      auto srcAgv = view<space>(fwdGrid);
      auto velAgv = view<space>(velocity);
      auto dstAgv = view<space>(bwdGrid);
      auto params = zs::make_tuple(srcAgv, velAgv, dstAgv, (size_type)sdfChannel,
                                   (size_type)velChannel, -dt);  // negative dt for backward
      pol(range(nbs * bs), params, AgAdvectMacCormackForwardKernel{});
    }

    // Step 3: Correction with clamping (φ* = φ̂ + 0.5*(φ - φ̃), clamped)
    // Use a separate output grid to avoid read-write conflict on neighbor lookups
    auto resultGrid = field.clone(field.memoryLocation());
    {
      auto origAgv = view<space>(field);
      auto fwdAgv = view<space>(fwdGrid);
      auto bwdAgv = view<space>(bwdGrid);
      auto velAgv = view<space>(velocity);
      auto dstAgv = view<space>(resultGrid);
      auto params = zs::make_tuple(origAgv, fwdAgv, bwdAgv, velAgv, dstAgv,
                                   (size_type)sdfChannel, (size_type)velChannel, dt);
      pol(range(nbs * bs), params, AgAdvectMacCormackCorrectionKernel{});
    }

    // Copy result back to field
    {
      auto srcAgv = view<space>(resultGrid);
      auto dstAgv = view<space>(field);
      pol(range(nbs * bs),
          [srcAgv, dstAgv, chn = (size_type)sdfChannel] ZS_LAMBDA(size_t i) mutable {
            auto &srcLev = srcAgv.level(dim_c<0>);
            auto &dstLev = dstAgv.level(dim_c<0>);
            constexpr auto bsz = RM_CVREF_T(srcLev)::block_size;
            auto bno = i / bsz;
            auto cno = i % bsz;
            if (srcLev.valueMask[bno].isOn(cno)) {
              dstLev.grid(chn, bno, cno) = srcLev.grid(chn, bno, cno);
            }
          });
    }
  }

  // ============================================================================
  //
  //  Multi-Resolution / Adaptive Operations (Bifrost-style)
  //
  //  Prolongation:   interpolate coarse-level data to fine-level voxels
  //  Restriction:    average fine-level data into coarse-level voxels
  //  Refinement:     create finer-level voxels where detail is needed
  //  Coarsening:     remove fine-level voxels where detail is low
  //
  // ============================================================================

  /// @brief Prolongation kernel — interpolate values from a coarser level to a finer level.
  /// For each active voxel at the fine level, trilinearly interpolate from the coarse level.
  template <int FineLevelNo, int CoarseLevelNo> struct AgProlongateKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, chn] = params;
      auto &fineLev = agv.level(dim_c<FineLevelNo>);
      auto &coarseLev = agv.level(dim_c<CoarseLevelNo>);
      constexpr auto fine_bs = RM_CVREF_T(fineLev)::block_size;
      constexpr int dim = RM_CVREF_T(agv)::dim;
      auto bno = i / fine_bs;
      auto cno = i % fine_bs;
      if (fineLev.valueMask[bno].isOff(cno)) return;

      using value_type = typename RM_CVREF_T(agv)::value_type;
      using integer_coord_type = typename RM_CVREF_T(agv)::integer_coord_type;

      auto fineCoord = fineLev.table._activeKeys[bno]
                       + RM_CVREF_T(agv)::template tile_offset_to_coord<FineLevelNo>(cno);

      // Compute the corresponding coarse coordinate
      // The scaling between levels is determined by the hierarchy bits
      constexpr auto fine_sbit = RM_CVREF_T(fineLev)::sbit;
      constexpr auto coarse_sbit = RM_CVREF_T(coarseLev)::sbit;
      constexpr auto shift = coarse_sbit - fine_sbit;

      // Map fine coord to coarse coord (integer division by 2^shift, rounding down)
      integer_coord_type coarseCoord{};
      for (int d = 0; d < dim; ++d) coarseCoord[d] = fineCoord[d] >> shift;

      // Look up value in coarse level
      auto coarseKey = RM_CVREF_T(agv)::template coord_to_key<CoarseLevelNo>(coarseCoord);
      auto coarseBno = coarseLev.table.query(coarseKey);
      if (coarseBno == RM_CVREF_T(coarseLev.table)::sentinel_v) return;

      auto coarseCno = RM_CVREF_T(agv)::template coord_to_tile_offset<CoarseLevelNo>(coarseCoord);
      if (coarseLev.valueMask[coarseBno].isOn(coarseCno)) {
        value_type coarseVal = coarseLev.grid(chn, coarseBno, coarseCno);
        fineLev.grid(chn, bno, cno) = coarseVal;
      }
    }
  };

  /// @brief Prolongate (inject) values from coarse level to fine level.
  /// This is a direct injection — each fine voxel gets the value from its parent coarse voxel.
  /// @tparam FineLevelNo The fine (child) level number (lower = finer)
  /// @tparam CoarseLevelNo The coarse (parent) level number (higher = coarser)
  template <int FineLevelNo, int CoarseLevelNo, typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_prolongate(ExecPol &&pol, AdaptiveGridT &grid, size_t channel) {
    static_assert(FineLevelNo < CoarseLevelNo, "Fine level must be lower than coarse level");
    using size_type = typename AdaptiveGridT::size_type;
    auto &fineLev = grid.level(dim_c<FineLevelNo>);
    auto nbs = fineLev.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<FineLevelNo>();

    auto agv = view<space>(grid);
    auto params = zs::make_tuple(agv, (size_type)channel);
    pol(range(nbs * bs), params, AgProlongateKernel<FineLevelNo, CoarseLevelNo>{});
  }

  /// @brief Trilinear prolongation kernel — interpolate values from a coarser level
  /// to a finer level using trilinear interpolation from the surrounding 2^dim
  /// coarse voxels. This is a Bifrost/multigrid-style enhancement over direct
  /// injection, producing smoother transitions at coarse/fine boundaries and
  /// improving multigrid convergence rates.
  template <int FineLevelNo, int CoarseLevelNo> struct AgProlongateTrilinearKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, chn] = params;
      auto &fineLev = agv.level(dim_c<FineLevelNo>);
      auto &coarseLev = agv.level(dim_c<CoarseLevelNo>);
      constexpr auto fine_bs = RM_CVREF_T(fineLev)::block_size;
      constexpr int dim = RM_CVREF_T(agv)::dim;
      auto bno = i / fine_bs;
      auto cno = i % fine_bs;
      if (fineLev.valueMask[bno].isOff(cno)) return;

      using value_type = typename RM_CVREF_T(agv)::value_type;
      using integer_coord_type = typename RM_CVREF_T(agv)::integer_coord_type;

      auto fineCoord = fineLev.table._activeKeys[bno]
                       + RM_CVREF_T(agv)::template tile_offset_to_coord<FineLevelNo>(cno);

      constexpr auto fine_sbit = RM_CVREF_T(fineLev)::sbit;
      constexpr auto coarse_sbit = RM_CVREF_T(coarseLev)::sbit;
      constexpr auto shift = coarse_sbit - fine_sbit;
      constexpr auto ratio = 1 << shift;

      // Compute fractional position within the coarse cell [0, 1)
      // The fine coordinate maps to coarse cell at (fineCoord >> shift)
      // The fractional offset within that coarse cell determines the interpolation weights
      value_type frac[dim];
      integer_coord_type coarseBase{};
      for (int d = 0; d < dim; ++d) {
        auto coarseIdx = fineCoord[d] >> shift;
        // Fractional position within coarse cell
        auto localFine = fineCoord[d] - (coarseIdx << shift);
        frac[d] = ((value_type)localFine + (value_type)0.5) / (value_type)ratio - (value_type)0.5;
        // If frac < 0, interpolate with the previous coarse cell
        coarseBase[d] = frac[d] < (value_type)0 ? coarseIdx - 1 : coarseIdx;
        if (frac[d] < (value_type)0) frac[d] += (value_type)1;
      }

      // Trilinear interpolation from 2^dim coarse voxels
      value_type result = (value_type)0;
      for (int iz = 0; iz < (dim > 2 ? 2 : 1); ++iz)
        for (int iy = 0; iy < (dim > 1 ? 2 : 1); ++iy)
          for (int ix = 0; ix < 2; ++ix) {
            integer_coord_type cc = coarseBase;
            cc[0] += ix;
            if constexpr (dim > 1) cc[1] += iy;
            if constexpr (dim > 2) cc[2] += iz;

            value_type w = ix ? frac[0] : (value_type)1 - frac[0];
            if constexpr (dim > 1) w *= iy ? frac[1] : (value_type)1 - frac[1];
            if constexpr (dim > 2) w *= iz ? frac[2] : (value_type)1 - frac[2];

            // Look up coarse value
            auto coarseKey = RM_CVREF_T(agv)::template coord_to_key<CoarseLevelNo>(cc);
            auto coarseBno = coarseLev.table.query(coarseKey);
            if (coarseBno != RM_CVREF_T(coarseLev.table)::sentinel_v) {
              auto coarseCno
                  = RM_CVREF_T(agv)::template coord_to_tile_offset<CoarseLevelNo>(cc);
              if (coarseLev.valueMask[coarseBno].isOn(coarseCno)) {
                result += w * coarseLev.grid(chn, coarseBno, coarseCno);
              } else {
                result += w * agv._background;
              }
            } else {
              result += w * agv._background;
            }
          }

      fineLev.grid(chn, bno, cno) = result;
    }
  };

  /// @brief Prolongate values from coarse to fine level using trilinear interpolation.
  /// Provides smoother transitions than injection and improves multigrid convergence.
  /// @tparam FineLevelNo The fine (child) level number (lower = finer)
  /// @tparam CoarseLevelNo The coarse (parent) level number (higher = coarser)
  template <int FineLevelNo, int CoarseLevelNo, typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_prolongate_trilinear(ExecPol &&pol, AdaptiveGridT &grid, size_t channel) {
    static_assert(FineLevelNo < CoarseLevelNo, "Fine level must be lower than coarse level");
    using size_type = typename AdaptiveGridT::size_type;
    auto &fineLev = grid.level(dim_c<FineLevelNo>);
    auto nbs = fineLev.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<FineLevelNo>();

    auto agv = view<space>(grid);
    auto params = zs::make_tuple(agv, (size_type)channel);
    pol(range(nbs * bs), params, AgProlongateTrilinearKernel<FineLevelNo, CoarseLevelNo>{});
  }

  /// @brief Restriction kernel — average fine-level values into coarse-level voxels.
  /// For each active coarse voxel that has children, compute the average of all
  /// overlapping fine-level voxels.
  template <int CoarseLevelNo, int FineLevelNo> struct AgRestrictKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, chn] = params;
      auto &coarseLev = agv.level(dim_c<CoarseLevelNo>);
      auto &fineLev = agv.level(dim_c<FineLevelNo>);
      constexpr auto coarse_bs = RM_CVREF_T(coarseLev)::block_size;
      constexpr int dim = RM_CVREF_T(agv)::dim;
      auto bno = i / coarse_bs;
      auto cno = i % coarse_bs;

      using value_type = typename RM_CVREF_T(agv)::value_type;
      using integer_coord_type = typename RM_CVREF_T(agv)::integer_coord_type;

      // Only process coarse voxels that have children
      auto hierarchyOff = RM_CVREF_T(agv)::template coord_to_hierarchy_offset<CoarseLevelNo>(
          coarseLev.table._activeKeys[bno]
          + RM_CVREF_T(agv)::template tile_offset_to_coord<CoarseLevelNo>(cno));
      if (coarseLev.childMask[bno].isOff(hierarchyOff)) return;

      auto coarseCoord = coarseLev.table._activeKeys[bno]
                         + RM_CVREF_T(agv)::template tile_offset_to_coord<CoarseLevelNo>(cno);

      constexpr auto fine_sbit = RM_CVREF_T(fineLev)::sbit;
      constexpr auto coarse_sbit = RM_CVREF_T(coarseLev)::sbit;
      constexpr auto shift = coarse_sbit - fine_sbit;
      constexpr auto ratio = 1 << shift;  // number of fine cells per coarse cell in each dim

      // Average all fine voxels that map to this coarse voxel
      value_type sum = (value_type)0;
      int count = 0;
      auto fineOrigin = coarseCoord;
      for (int d = 0; d < dim; ++d) fineOrigin[d] <<= shift;

      for (int iz = 0; iz < (dim > 2 ? ratio : 1); ++iz)
        for (int iy = 0; iy < (dim > 1 ? ratio : 1); ++iy)
          for (int ix = 0; ix < ratio; ++ix) {
            integer_coord_type fineCoord = fineOrigin;
            fineCoord[0] += ix;
            if constexpr (dim > 1) fineCoord[1] += iy;
            if constexpr (dim > 2) fineCoord[2] += iz;

            auto fineKey = RM_CVREF_T(agv)::template coord_to_key<FineLevelNo>(fineCoord);
            auto fineBno = fineLev.table.query(fineKey);
            if (fineBno == RM_CVREF_T(fineLev.table)::sentinel_v) continue;
            auto fineCno = RM_CVREF_T(agv)::template coord_to_tile_offset<FineLevelNo>(fineCoord);
            if (fineLev.valueMask[fineBno].isOn(fineCno)) {
              sum += fineLev.grid(chn, fineBno, fineCno);
              ++count;
            }
          }

      if (count > 0) {
        coarseLev.grid(chn, bno, cno) = sum / (value_type)count;
        coarseLev.valueMask[bno].setOn(cno, wrapv<RM_CVREF_T(agv)::space>{});
      }
    }
  };

  /// @brief Restrict (average) values from fine level to coarse level.
  /// Each coarse voxel with children receives the average of its overlapping fine voxels.
  template <int CoarseLevelNo, int FineLevelNo, typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_restrict(ExecPol &&pol, AdaptiveGridT &grid, size_t channel) {
    static_assert(FineLevelNo < CoarseLevelNo, "Fine level must be lower than coarse level");
    using size_type = typename AdaptiveGridT::size_type;
    auto &coarseLev = grid.level(dim_c<CoarseLevelNo>);
    auto nbs = coarseLev.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<CoarseLevelNo>();

    auto agv = view<space>(grid);
    auto params = zs::make_tuple(agv, (size_type)channel);
    pol(range(nbs * bs), params, AgRestrictKernel<CoarseLevelNo, FineLevelNo>{});
  }

  /// @brief Adaptive refinement kernel — mark coarse voxels for refinement
  /// based on a user-supplied criterion (e.g., gradient magnitude, curvature).
  /// Voxels exceeding the threshold are flagged for subdivision.
  struct AgRefineMarkKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, chn, threshold, refineFlags] = params;
      auto &lev = agv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(lev)::block_size;
      constexpr int dim = RM_CVREF_T(agv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (lev.valueMask[bno].isOff(cno)) return;

      auto coord = lev.table._activeKeys[bno]
                   + RM_CVREF_T(agv)::template tile_offset_to_coord<0>(cno);

      // Compute gradient magnitude as refinement criterion
      using value_type = typename RM_CVREF_T(agv)::value_type;
      auto acc = agv.getAccessor();
      value_type gradMag2 = (value_type)0;
      for (int d = 0; d < dim; ++d) {
        auto cP = coord, cM = coord;
        cP[d] += 1;
        cM[d] -= 1;
        value_type vp{}, vm{};
        acc.probeValue(chn, cP, vp);
        acc.probeValue(chn, cM, vm);
        auto grad = (vp - vm) * (value_type)0.5;
        gradMag2 += grad * grad;
      }
      refineFlags[i] = zs::sqrt(gradMag2) > threshold ? 1 : 0;
    }
  };

  /// @brief Adaptively refine the grid based on gradient magnitude.
  /// Leaf-level voxels whose gradient magnitude exceeds the threshold are
  /// marked for refinement. The grid is then restructured to a new grid
  /// with one additional level of resolution where needed.
  ///
  /// This is a Bifrost-style operation: regions of high detail get finer
  /// resolution while smooth regions remain at coarser resolution.
  ///
  /// @param grid The adaptive grid to refine
  /// @param channel The channel to evaluate for refinement
  /// @param threshold Gradient magnitude threshold for refinement
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_mark_refine(ExecPol &&pol, const AdaptiveGridT &grid, size_t channel,
                      typename AdaptiveGridT::value_type threshold,
                      Vector<int> &refineFlags) {
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    auto totalCells = nbs * bs;

    refineFlags.resize(totalCells);
    pol(range(totalCells),
        [flags = view<space>(refineFlags)] ZS_LAMBDA(size_t i) mutable { flags[i] = 0; });

    auto agv = view<space>(grid);
    auto params = zs::make_tuple(agv, (size_type)channel, threshold, view<space>(refineFlags));
    pol(range(totalCells), params, AgRefineMarkKernel{});
  }

  /// @brief Coarsening criterion kernel — mark fine voxels for removal
  /// when local variation is below threshold.
  struct AgCoarsenMarkKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[agv, chn, threshold, coarsenFlags] = params;
      auto &lev = agv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(lev)::block_size;
      constexpr int dim = RM_CVREF_T(agv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (lev.valueMask[bno].isOff(cno)) return;

      auto coord = lev.table._activeKeys[bno]
                   + RM_CVREF_T(agv)::template tile_offset_to_coord<0>(cno);

      using value_type = typename RM_CVREF_T(agv)::value_type;
      auto acc = agv.getAccessor();
      value_type gradMag2 = (value_type)0;
      for (int d = 0; d < dim; ++d) {
        auto cP = coord, cM = coord;
        cP[d] += 1;
        cM[d] -= 1;
        value_type vp{}, vm{};
        acc.probeValue(chn, cP, vp);
        acc.probeValue(chn, cM, vm);
        auto grad = (vp - vm) * (value_type)0.5;
        gradMag2 += grad * grad;
      }
      // Mark for coarsening if gradient is below threshold
      coarsenFlags[i] = zs::sqrt(gradMag2) < threshold ? 1 : 0;
    }
  };

  /// @brief Mark leaf voxels for coarsening where gradient magnitude is below threshold.
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_mark_coarsen(ExecPol &&pol, const AdaptiveGridT &grid, size_t channel,
                       typename AdaptiveGridT::value_type threshold,
                       Vector<int> &coarsenFlags) {
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    auto totalCells = nbs * bs;

    coarsenFlags.resize(totalCells);
    pol(range(totalCells),
        [flags = view<space>(coarsenFlags)] ZS_LAMBDA(size_t i) mutable { flags[i] = 0; });

    auto agv = view<space>(grid);
    auto params = zs::make_tuple(agv, (size_type)channel, threshold, view<space>(coarsenFlags));
    pol(range(totalCells), params, AgCoarsenMarkKernel{});
  }

  // ============================================================================
  //
  //  Resampling
  //
  //  Sample values from a source grid onto a destination grid's topology
  //  using the source grid's interpolation (iSample).
  //
  // ============================================================================

  /// @brief Resample kernel — for each active voxel in dst, sample the src grid
  struct AgResampleKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, dstAgv, chn] = params;
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(dstLev)::block_size;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (dstLev.valueMask[bno].isOff(cno)) return;

      using value_type = typename RM_CVREF_T(srcAgv)::value_type;
      auto coord = dstLev.table._activeKeys[bno]
                   + RM_CVREF_T(dstAgv)::template tile_offset_to_coord<0>(cno);

      // Convert dst index coord to world, then to src index coord
      auto worldPos = dstAgv.indexToWorld(coord.template cast<value_type>());
      auto srcIndexPos = srcAgv.worldToIndex(worldPos);

      dstLev.grid(chn, bno, cno) = srcAgv.iSample(chn, srcIndexPos);
    }
  };

  /// @brief Resample a source grid onto the destination grid's topology.
  /// For each active voxel in dst, the value is interpolated from src using
  /// world-space coordinate mapping and trilinear interpolation.
  /// @param src Source grid to sample from
  /// @param dst Destination grid (topology must already be established)
  /// @param channel Channel to resample
  template <typename ExecPol, typename SrcGridT, typename DstGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_resample(ExecPol &&pol, const SrcGridT &src, DstGridT &dst, size_t channel) {
    using size_type = typename DstGridT::size_type;
    auto &dstLev0 = dst.level(dim_c<0>);
    auto nbs = dstLev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = DstGridT::template get_tile_size<0>();

    auto srcAgv = view<space>(src);
    auto dstAgv = view<space>(dst);
    auto params = zs::make_tuple(srcAgv, dstAgv, (size_type)channel);
    pol(range(nbs * bs), params, AgResampleKernel{});
  }

  // ============================================================================
  //
  //  Normalization / Utility
  //
  // ============================================================================

  /// @brief Normalize an SDF so that |grad(phi)| = 1 (Eikonal equation).
  /// Uses one step of the reinitialization PDE:
  ///   phi^{n+1} = phi^n - dt * S(phi^0) * (|grad(phi^n)| - 1)
  /// where S is a smoothed sign function.
  struct AgReinitializeKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, dstAgv, sdfChn, dt] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;

      using value_type = typename RM_CVREF_T(srcAgv)::value_type;
      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
      auto acc = srcAgv.getAccessor();

      auto phi = srcLev.grid(sdfChn, bno, cno);

      // Godunov upwind scheme for |grad(phi)|
      value_type gradMag2 = (value_type)0;
      for (int d = 0; d < dim; ++d) {
        auto cP = coord, cM = coord;
        cP[d] += 1;
        cM[d] -= 1;
        value_type vp{}, vm{};
        acc.probeValue(sdfChn, cP, vp);
        acc.probeValue(sdfChn, cM, vm);

        // Forward and backward differences
        value_type Dp = vp - phi;
        value_type Dm = phi - vm;

        if (phi > (value_type)0) {
          // Upwind: use max of backwards, min of forwards
          value_type a = Dm > (value_type)0 ? Dm : (value_type)0;
          value_type b = Dp < (value_type)0 ? -Dp : (value_type)0;
          gradMag2 += (a > b ? a * a : b * b);
        } else {
          value_type a = Dm < (value_type)0 ? -Dm : (value_type)0;
          value_type b = Dp > (value_type)0 ? Dp : (value_type)0;
          gradMag2 += (a > b ? a * a : b * b);
        }
      }

      value_type gradMag = zs::sqrt(gradMag2);

      // Smoothed sign function
      value_type eps = (value_type)1;
      value_type S = phi / zs::sqrt(phi * phi + eps * eps);

      dstLev.grid(sdfChn, bno, cno) = phi - dt * S * (gradMag - (value_type)1);
    }
  };

  /// @brief Reinitialize a level set SDF to satisfy |grad(phi)| = 1.
  /// Uses Fast Marching-inspired iterative PDE reinitialization.
  /// @param grid The level set grid
  /// @param sdfChannel The SDF channel
  /// @param iterations Number of reinitialization iterations
  /// @param dt CFL-stable time step (default: 0.5 * dx)
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_reinitialize(ExecPol &&pol, AdaptiveGridT &grid, size_t sdfChannel = 0,
                       int iterations = 5,
                       typename AdaptiveGridT::value_type dt = -1) {
    using value_type = typename AdaptiveGridT::value_type;
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

    if (dt < (value_type)0) dt = (value_type)0.5 * grid.voxelSize()[0];

    auto tmpGrid = grid.clone(grid.memoryLocation());

    for (int iter = 0; iter < iterations; ++iter) {
      auto &srcGrid = (iter % 2 == 0) ? grid : tmpGrid;
      auto &dstGrid = (iter % 2 == 0) ? tmpGrid : grid;
      auto srcAgv = view<space>(srcGrid);
      auto dstAgv = view<space>(dstGrid);
      auto params = zs::make_tuple(srcAgv, dstAgv, (size_type)sdfChannel, dt);
      pol(range(nbs * bs), params, AgReinitializeKernel{});
    }

    if (iterations % 2 == 1) {
      auto srcAgv = view<space>(tmpGrid);
      auto dstAgv = view<space>(grid);
      pol(range(nbs * bs),
          [srcAgv, dstAgv, chn = (size_type)sdfChannel] ZS_LAMBDA(size_t i) mutable {
            auto &srcLev = srcAgv.level(dim_c<0>);
            auto &dstLev = dstAgv.level(dim_c<0>);
            constexpr auto bsz = RM_CVREF_T(srcLev)::block_size;
            auto bno = i / bsz;
            auto cno = i % bsz;
            if (srcLev.valueMask[bno].isOn(cno)) {
              dstLev.grid(chn, bno, cno) = srcLev.grid(chn, bno, cno);
            }
          });
    }
  }

  // ============================================================================
  //
  //  Narrow Band Management (OpenVDB-style)
  //
  //  OpenVDB maintains a narrow band of active voxels around the zero-crossing
  //  of a level set. Voxels outside this band are deactivated to save both
  //  memory and computation. These operations bring similar capability:
  //
  //  - ag_trim_narrow_band: Remove voxels with |phi| > halfWidth * dx
  //  - ag_ensure_narrow_band: Dilate topology to guarantee band width
  //
  // ============================================================================

  /// @brief Trim a level set to a narrow band: deactivate and reset all voxels
  /// whose absolute SDF value exceeds halfWidth * dx.
  /// This is critical for performance — after CSG or advection, many voxels may
  /// lie far from the interface and waste computation in subsequent operations.
  /// @param grid The level set grid
  /// @param sdfChannel The SDF channel
  /// @param halfWidth The half-width of the narrow band in voxels (e.g., 3.0)
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_trim_narrow_band(ExecPol &&pol, AdaptiveGridT &grid, size_t sdfChannel = 0,
                           typename AdaptiveGridT::value_type halfWidth = 3) {
    using value_type = typename AdaptiveGridT::value_type;
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    auto totalCells = nbs * bs;

    auto dx = grid.voxelSize()[0];
    auto maxDist = halfWidth * dx;
    auto bg = grid._background;

    auto agv = view<space>(grid);
    pol(range(totalCells),
        [agv, chn = (size_type)sdfChannel, maxDist, bg] ZS_LAMBDA(size_t i) mutable {
          auto &lev = agv.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(lev)::block_size;
          auto bno = i / bsz;
          auto cno = i % bsz;
          if (lev.valueMask[bno].isOff(cno)) return;

          auto val = lev.grid(chn, bno, cno);
          auto absVal = val < (decltype(val))0 ? -val : val;
          if (absVal > maxDist) {
            // Deactivate and set to signed background
            lev.valueMask[bno].setOff(cno, wrapv<RM_CVREF_T(agv)::space>{});
            auto absBg = bg < (decltype(bg))0 ? -bg : bg;
            for (int d = 0; d < (int)lev.numChannels(); ++d)
              lev.grid(d, bno, cno) = val < (decltype(val))0 ? -absBg : absBg;
          }
        });
  }

  /// @brief Ensure the narrow band has sufficient topology for operations.
  /// Dilates the topology by the specified number of voxels and initializes
  /// the newly activated voxels with the signed background value based on
  /// their nearest neighbor's sign.
  /// @param grid The level set grid
  /// @param sdfChannel The SDF channel
  /// @param dilationWidth Number of voxels to dilate (typically = halfWidth)
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_ensure_narrow_band(ExecPol &&pol, AdaptiveGridT &grid, size_t sdfChannel = 0,
                             int dilationWidth = 3) {
    using value_type = typename AdaptiveGridT::value_type;
    using size_type = typename AdaptiveGridT::size_type;

    // Dilate topology to ensure sufficient band width
    ag_dilate(FWD(pol), grid, dilationWidth);

    // Initialize newly activated voxels: they currently have default (0) values.
    // Set them to +/- background based on the sign of their nearest active neighbor.
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    constexpr int dim = AdaptiveGridT::dim;
    auto totalCells = nbs * bs;
    auto bg = grid._background;
    auto absBg = bg < (value_type)0 ? -bg : bg;

    auto agv = view<space>(grid);
    pol(range(totalCells),
        [agv, chn = (size_type)sdfChannel, absBg] ZS_LAMBDA(size_t i) mutable {
          auto &lev = agv.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(lev)::block_size;
          constexpr int d = RM_CVREF_T(agv)::dim;
          auto bno = i / bsz;
          auto cno = i % bsz;
          // Only process voxels that are active but have zero/default value
          if (lev.valueMask[bno].isOff(cno)) return;
          auto val = lev.grid(chn, bno, cno);
          // If already has a meaningful value, skip
          auto absVal = val < (decltype(val))0 ? -val : val;
          if (absVal > (decltype(val))1e-10) return;

          // Find sign from nearest neighbor with nonzero value
          auto coord = lev.table._activeKeys[bno]
                       + RM_CVREF_T(agv)::template tile_offset_to_coord<0>(cno);
          auto acc = agv.getAccessor();
          for (int axis = 0; axis < d; ++axis) {
            for (int dir = -1; dir <= 1; dir += 2) {
              auto nc = coord;
              nc[axis] += dir;
              decltype(val) nv{};
              if (acc.probeValue(chn, nc, nv)) {
                auto absNv = nv < (decltype(nv))0 ? -nv : nv;
                if (absNv > (decltype(nv))1e-10) {
                  lev.grid(chn, bno, cno) = nv < (decltype(nv))0 ? -absBg : absBg;
                  return;
                }
              }
            }
          }
          // Default to positive background
          lev.grid(chn, bno, cno) = absBg;
        });
  }

  // ============================================================================
  //
  //  SDF Value Extrapolation
  //
  //  After advection or topology changes, newly activated voxels may have
  //  uninitialized values. This operation propagates valid SDF values outward
  //  from the narrow band into the surrounding voxels, maintaining correct
  //  sign and approximate distance.
  //
  //  OpenVDB performs this via openvdb::tools::extrapolateIntoBackground.
  //
  // ============================================================================

  /// @brief Extrapolation kernel — for each voxel with invalid SDF, copy the
  /// average value from its active neighbors that have valid SDF values.
  struct AgExtrapolateKernel {
    template <typename ParamT> constexpr void operator()(size_t i, ParamT &&params) const {
      auto &[srcAgv, dstAgv, chn, validFlags, changed] = params;
      auto &srcLev = srcAgv.level(dim_c<0>);
      auto &dstLev = dstAgv.level(dim_c<0>);
      constexpr auto block_size = RM_CVREF_T(srcLev)::block_size;
      constexpr int dim = RM_CVREF_T(srcAgv)::dim;
      auto bno = i / block_size;
      auto cno = i % block_size;
      if (srcLev.valueMask[bno].isOff(cno)) return;
      if (validFlags[i]) return;  // already valid

      auto coord = srcLev.table._activeKeys[bno]
                   + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
      auto acc = srcAgv.getAccessor();
      using value_type = typename RM_CVREF_T(srcAgv)::value_type;

      value_type sum = (value_type)0;
      int count = 0;
      for (int axis = 0; axis < dim; ++axis) {
        for (int dir = -1; dir <= 1; dir += 2) {
          auto nc = coord;
          nc[axis] += dir;
          auto key = RM_CVREF_T(srcAgv)::template coord_to_key<0>(nc);
          auto nBno = srcLev.table.query(key);
          if (nBno == RM_CVREF_T(srcLev.table)::sentinel_v) continue;
          auto nCno = RM_CVREF_T(srcAgv)::template coord_to_tile_offset<0>(nc);
          if (srcLev.valueMask[nBno].isOff(nCno)) continue;
          auto nIdx = (size_t)nBno * block_size + nCno;
          if (validFlags[nIdx]) {
            sum += srcLev.grid(chn, nBno, nCno);
            ++count;
          }
        }
      }
      if (count > 0) {
        dstLev.grid(chn, bno, cno) = sum / (value_type)count;
        // Mark as newly valid (will be committed after this iteration)
        atomic_add(wrapv<RM_CVREF_T(srcAgv)::space>{}, changed, 1);
      }
    }
  };

  /// @brief Extrapolate SDF values from valid narrow-band voxels outward into
  /// the surrounding active topology. Uses iterative neighbor averaging.
  /// @param grid The level set grid
  /// @param sdfChannel The SDF channel
  /// @param iterations Maximum number of extrapolation layers
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_extrapolate(ExecPol &&pol, AdaptiveGridT &grid, size_t sdfChannel = 0,
                      int iterations = 3) {
    using value_type = typename AdaptiveGridT::value_type;
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    auto totalCells = nbs * bs;
    auto bg = grid._background;
    auto absBg = bg < (value_type)0 ? -bg : bg;

    auto allocator = get_temporary_memory_source(pol);
    Vector<int> validFlags{allocator, totalCells};
    Vector<int> newValid{allocator, totalCells};
    Vector<int> changed{allocator, 1};

    auto agv = view<space>(grid);

    // Initialize valid flags: voxels in narrow band are valid
    pol(range(totalCells),
        [agv, chn = (size_type)sdfChannel, vf = view<space>(validFlags),
         absBg] ZS_LAMBDA(size_t i) mutable {
          auto &lev = agv.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(lev)::block_size;
          auto bno = i / bsz;
          auto cno = i % bsz;
          if (lev.valueMask[bno].isOn(cno)) {
            auto val = lev.grid(chn, bno, cno);
            auto absVal = val < (decltype(val))0 ? -val : val;
            vf[i] = absVal < absBg ? 1 : 0;
          } else {
            vf[i] = 0;
          }
        });

    auto tmpGrid = grid.clone(grid.memoryLocation());

    for (int iter = 0; iter < iterations; ++iter) {
      pol(range(1),
          [ch = view<space>(changed)] ZS_LAMBDA(size_t) mutable { ch[0] = 0; });
      pol(range(totalCells),
          [nv = view<space>(newValid)] ZS_LAMBDA(size_t i) mutable { nv[i] = 0; });

      auto srcAgv = view<space>(grid);
      auto dstAgv = view<space>(tmpGrid);
      auto params = zs::make_tuple(srcAgv, dstAgv, (size_type)sdfChannel,
                                   view<space>(validFlags), view<space>(changed).data());
      pol(range(totalCells), params, AgExtrapolateKernel{});

      if (changed.getVal(0) == 0) break;

      // Copy extrapolated values back and update valid flags
      pol(range(totalCells),
          [srcV = view<space>(tmpGrid), dstV = view<space>(grid),
           vfOld = view<space>(validFlags), chn = (size_type)sdfChannel,
           absBg] ZS_LAMBDA(size_t i) mutable {
            auto &srcLev = srcV.level(dim_c<0>);
            auto &dstLev = dstV.level(dim_c<0>);
            constexpr auto bsz = RM_CVREF_T(srcLev)::block_size;
            auto bno = i / bsz;
            auto cno = i % bsz;
            if (srcLev.valueMask[bno].isOff(cno)) return;
            if (vfOld[i]) return;  // already valid, skip
            auto val = srcLev.grid(chn, bno, cno);
            auto absVal = val < (decltype(val))0 ? -val : val;
            if (absVal > (decltype(val))1e-20) {
              dstLev.grid(chn, bno, cno) = val;
              vfOld[i] = 1;
            }
          });
    }
  }

  // ============================================================================
  //
  //  Topology Combine Operations (OpenVDB-style)
  //
  // ============================================================================

  /// @brief Union topology from src into dst: activate all voxels in dst
  /// that are active in src (without modifying existing values in dst).
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_topology_union(ExecPol &&pol, AdaptiveGridT &dst, const AdaptiveGridT &src) {
    using size_type = typename AdaptiveGridT::size_type;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();

    auto &dstLev0 = dst.level(dim_c<0>);
    const auto &srcLev0 = src.level(dim_c<0>);
    auto srcNbs = srcLev0.numBlocks();
    auto prevDstNbs = dstLev0.numBlocks();

    // Insert all source leaf keys
    dstLev0.resizePartition(pol, prevDstNbs + srcNbs);
    pol(range(srcNbs), [srcKeys = view<space>(srcLev0.table._activeKeys),
                        dstTab = view<space>(dstLev0.table)] ZS_LAMBDA(size_type i) mutable {
      dstTab.insert(srcKeys[i]);
    });

    auto newDstNbs = dstLev0.numBlocks();
    if (newDstNbs > prevDstNbs) {
      dstLev0.refitToPartition();
      // Zero-initialize only the newly added blocks (not the existing ones)
      for (auto bi = prevDstNbs; bi < newDstNbs; ++bi) {
        dstLev0.valueMask[bi].setOff();
        dstLev0.childMask[bi].setOff();
      }
      auto gridV = view<space>(dstLev0.grid);
      auto numCh = dstLev0.grid.numChannels();
      for (size_t bi = prevDstNbs; bi < (size_t)newDstNbs; ++bi)
        for (int ch = 0; ch < numCh; ++ch)
          for (size_t ci = 0; ci < bs; ++ci)
            gridV(ch, bi, ci) = 0;
      dst.complementTopo(pol);
    }

    // Activate voxels in dst that are active in src
    auto dstNbs = dstLev0.numBlocks();
    auto srcAgv = view<space>(src);
    auto dstAgv = view<space>(dst);
    pol(range(srcNbs * bs),
        [srcAgv, dstAgv] ZS_LAMBDA(size_t i) mutable {
          auto &srcLev = srcAgv.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(srcLev)::block_size;
          auto bno = i / bsz;
          auto cno = i % bsz;
          if (srcLev.valueMask[bno].isOff(cno)) return;

          auto coord = srcLev.table._activeKeys[bno]
                       + RM_CVREF_T(srcAgv)::template tile_offset_to_coord<0>(cno);
          auto &dstLev = dstAgv.level(dim_c<0>);
          auto dstKey = RM_CVREF_T(dstAgv)::template coord_to_key<0>(coord);
          auto dstBno = dstLev.table.query(dstKey);
          if (dstBno != RM_CVREF_T(dstLev.table)::sentinel_v) {
            auto dstCno = RM_CVREF_T(dstAgv)::template coord_to_tile_offset<0>(coord);
            dstLev.valueMask[dstBno].setOn(dstCno, wrapv<RM_CVREF_T(dstAgv)::space>{});
          }
        });
  }

  /// @brief Intersect topology: deactivate voxels in dst that are NOT active in src.
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_topology_intersection(ExecPol &&pol, AdaptiveGridT &dst, const AdaptiveGridT &src) {
    using size_type = typename AdaptiveGridT::size_type;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    auto &dstLev0 = dst.level(dim_c<0>);
    auto dstNbs = dstLev0.numBlocks();
    if (dstNbs == 0) return;

    auto srcAgv = view<space>(src);
    auto dstAgv = view<space>(dst);
    auto bg = dst._background;

    pol(range(dstNbs * bs),
        [srcAgv, dstAgv, bg] ZS_LAMBDA(size_t i) mutable {
          auto &dstLev = dstAgv.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(dstLev)::block_size;
          auto bno = i / bsz;
          auto cno = i % bsz;
          if (dstLev.valueMask[bno].isOff(cno)) return;

          auto coord = dstLev.table._activeKeys[bno]
                       + RM_CVREF_T(dstAgv)::template tile_offset_to_coord<0>(cno);

          // Check if src has this voxel active
          using value_type = typename RM_CVREF_T(srcAgv)::value_type;
          value_type srcVal{};
          auto acc = srcAgv.getAccessor();
          bool found = acc.probeValue(0, coord, srcVal);
          if (!found) {
            // Deactivate in dst
            dstLev.valueMask[bno].setOff(cno, wrapv<RM_CVREF_T(dstAgv)::space>{});
            for (int d = 0; d < (int)dstLev.numChannels(); ++d) dstLev.grid(d, bno, cno) = bg;
          }
        });
  }

  /// @brief Subtract topology: deactivate voxels in dst that ARE active in src.
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  void ag_topology_difference(ExecPol &&pol, AdaptiveGridT &dst, const AdaptiveGridT &src) {
    using size_type = typename AdaptiveGridT::size_type;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    auto &dstLev0 = dst.level(dim_c<0>);
    auto dstNbs = dstLev0.numBlocks();
    if (dstNbs == 0) return;

    auto srcAgv = view<space>(src);
    auto dstAgv = view<space>(dst);
    auto bg = dst._background;

    pol(range(dstNbs * bs),
        [srcAgv, dstAgv, bg] ZS_LAMBDA(size_t i) mutable {
          auto &dstLev = dstAgv.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(dstLev)::block_size;
          auto bno = i / bsz;
          auto cno = i % bsz;
          if (dstLev.valueMask[bno].isOff(cno)) return;

          auto coord = dstLev.table._activeKeys[bno]
                       + RM_CVREF_T(dstAgv)::template tile_offset_to_coord<0>(cno);

          using value_type = typename RM_CVREF_T(srcAgv)::value_type;
          value_type srcVal{};
          auto acc = srcAgv.getAccessor();
          bool found = acc.probeValue(0, coord, srcVal);
          if (found) {
            dstLev.valueMask[bno].setOff(cno, wrapv<RM_CVREF_T(dstAgv)::space>{});
            for (int d = 0; d < (int)dstLev.numChannels(); ++d) dstLev.grid(d, bno, cno) = bg;
          }
        });
  }

  // ============================================================================
  //
  //  Extrema / Reduction
  //
  // ============================================================================

  /// @brief Compute min and max active values at the leaf level for a given channel.
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  auto ag_minmax(ExecPol &&pol, const AdaptiveGridT &grid, size_t channel)
      -> zs::tuple<typename AdaptiveGridT::value_type, typename AdaptiveGridT::value_type> {
    using value_type = typename AdaptiveGridT::value_type;
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return {grid._background, grid._background};
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    auto totalCells = nbs * bs;

    auto allocator = get_temporary_memory_source(pol);
    Vector<value_type> minVals{allocator, totalCells};
    Vector<value_type> maxVals{allocator, totalCells};

    auto agv = view<space>(grid);
    auto bg = grid._background;
    auto absBg = bg < (value_type)0 ? -bg : bg;
    auto hugeval = absBg * (value_type)1e6;

    pol(range(totalCells),
        [agv, chn = (size_type)channel, minV = view<space>(minVals), maxV = view<space>(maxVals),
         hugeval] ZS_LAMBDA(size_t i) mutable {
          auto &lev = agv.level(dim_c<0>);
          constexpr auto bsz = RM_CVREF_T(lev)::block_size;
          auto bno = i / bsz;
          auto cno = i % bsz;
          if (lev.valueMask[bno].isOn(cno)) {
            auto val = lev.grid(chn, bno, cno);
            minV[i] = val;
            maxV[i] = val;
          } else {
            minV[i] = hugeval;
            maxV[i] = -hugeval;
          }
        });

    Vector<value_type> minResult{allocator, 1};
    Vector<value_type> maxResult{allocator, 1};
    reduce(pol, minVals.begin(), minVals.end(), minResult.begin(), hugeval,
           getmin<value_type>{});
    reduce(pol, maxVals.begin(), maxVals.end(), maxResult.begin(), -hugeval,
           getmax<value_type>{});

    return {minResult.getVal(0), maxResult.getVal(0)};
  }

  /// @brief Count the number of active voxels at the leaf level.
  template <typename ExecPol, typename AdaptiveGridT,
            execspace_e space = remove_reference_t<ExecPol>::exec_tag::value>
  auto ag_count_active(ExecPol &&pol, const AdaptiveGridT &grid) ->
      typename AdaptiveGridT::size_type {
    using size_type = typename AdaptiveGridT::size_type;
    auto &lev0 = grid.level(dim_c<0>);
    auto nbs = lev0.numBlocks();
    if (nbs == 0) return 0;
    constexpr auto bs = AdaptiveGridT::template get_tile_size<0>();
    auto totalCells = nbs * bs;

    auto allocator = get_temporary_memory_source(pol);
    Vector<size_type> counts{allocator, totalCells};

    auto agv = view<space>(grid);
    pol(range(totalCells), [agv, cnt = view<space>(counts)] ZS_LAMBDA(size_t i) mutable {
      auto &lev = agv.level(dim_c<0>);
      constexpr auto bsz = RM_CVREF_T(lev)::block_size;
      auto bno = i / bsz;
      auto cno = i % bsz;
      cnt[i] = lev.valueMask[bno].isOn(cno) ? (size_type)1 : (size_type)0;
    });

    Vector<size_type> result{allocator, 1};
    reduce(pol, counts.begin(), counts.end(), result.begin());
    return result.getVal(0);
  }

}  // namespace zs