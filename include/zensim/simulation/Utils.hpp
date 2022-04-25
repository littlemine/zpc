#pragma once
#include "zensim/math/Vec.h"
#include "zensim/math/curve/InterpolationKernel.hpp"
#include "zensim/zpc_tpls/magic_enum/magic_enum.hpp"
#include "zensim/types/Iterator.h"
#include "zensim/types/Property.h"

namespace zs {

#if 0
  template <typename Tn, int dim, typename Ti, typename Table>
  constexpr auto unpack_coord_in_grid(const vec<Tn, dim> &coord, Ti sideLength,
                                      const Table &table) {
    using IV = vec<Tn, dim>;
    IV blockCoord = coord;
    for (int d = 0; d < dim; ++d) blockCoord[d] += (coord[d] < 0 ? -(Tn)sideLength + 1 : 0);
    blockCoord = blockCoord / (Tn)sideLength;
    return std::make_tuple(table.query(blockCoord), coord - blockCoord * (Tn)sideLength);
  }
  template <typename Tn, int dim, typename Ti>
  constexpr auto unpack_coord_in_grid(const vec<Tn, dim> &coord, Ti sideLength) {
    using IV = vec<Tn, dim>;
    IV blockCoord = coord;
    for (int d = 0; d < dim; ++d) blockCoord[d] += (coord[d] < 0 ? -(Tn)sideLength + 1 : 0);
    blockCoord = blockCoord / (Tn)sideLength;
    return std::make_tuple(blockCoord, coord - blockCoord * (Tn)sideLength);
  }
  template <typename Tn, int dim, typename Ti, typename Table, typename Grid>
  constexpr auto unpack_coord_in_grid(const vec<Tn, dim> &coord, Ti sideLength, const Table &table,
                                      Grid &&grid) {
    using IV = vec<Tn, dim>;
    IV blockCoord = coord;
    for (int d = 0; d < dim; ++d) blockCoord[d] += (coord[d] < 0 ? -(Tn)sideLength + 1 : 0);
    blockCoord = blockCoord / (Tn)sideLength;
    return std::forward_as_tuple(grid[table.query(blockCoord)],
                                 coord - blockCoord * (Tn)sideLength);
  }
#else
  template <typename Tn, int dim, typename Ti>
  constexpr auto unpack_coord_in_grid(const vec<Tn, dim> &coord, Ti sideLength) {
    auto loc = coord & (Tn)(sideLength - 1);
    return std::make_tuple((coord - loc) / (Tn)sideLength, loc);
  }
  template <typename Tn, int dim, typename Ti, typename Table>
  constexpr auto unpack_coord_in_grid(const vec<Tn, dim> &coord, Ti sideLength,
                                      const Table &table) {
    auto loc = coord & (Tn)(sideLength - 1);
    return std::make_tuple(table.query((coord - loc) / (Tn)sideLength), loc);
  }
  template <typename Tn, int dim, typename Ti, typename Table, typename Grid>
  constexpr auto unpack_coord_in_grid(const vec<Tn, dim> &coord, Ti sideLength, const Table &table,
                                      Grid &&grid) {
    auto loc = coord & (Tn)(sideLength - 1);
    if constexpr (std::is_reference_v<decltype(grid[0])>)
      return std::make_tuple(std::ref(grid[table.query((coord - loc) / (Tn)sideLength)]), loc);
    else
      return std::make_tuple(grid[table.query((coord - loc) / (Tn)sideLength)], loc);
  }
#endif

  template <grid_e gt = grid_e::collocated, kernel_e kt = kernel_e::quadratic, int drv_order = 0,
            typename T = f32, int dim_ = 3, typename Ti = int>
  struct LocalArena {
    using value_type = T;
    using index_type = Ti;
    static constexpr int dim = dim_;
    static constexpr index_type width = magic_enum::enum_integer(kt);
    static constexpr int deriv_order = drv_order;
    using TV = vec<value_type, dim>;
    using TM = vec<value_type, dim, width>;
    using IV = vec<index_type, dim>;

    static_assert(deriv_order >= 0 && deriv_order <= 2,
                  "weight derivative order should be a integer within [0, 2]");
    using WeightScratchPad
        = conditional_t<deriv_order == 0, tuple<TM>,
                        conditional_t<deriv_order == 1, tuple<TM, TM>, tuple<TM, TM, TM>>>;

    constexpr void init(const value_type dx_, const TV &pos) {
      dx = dx_;
      auto X = pos;
      if constexpr (gt == grid_e::cellcentered)
        X = pos / dx - (value_type)0.5;
      else
        X = pos / dx;

      constexpr int lerp_degree
          = (kt == kernel_e::linear ? 0 : (kt == kernel_e::quadratic ? 1 : 2));
      for (int d = 0; d != dim; ++d) corner[d] = base_node<lerp_degree>(X[d]);
      localPos = X - corner;
      if constexpr (kt == kernel_e::linear)
        weights = linear_bspline_weights<deriv_order>(localPos);
      else if constexpr (kt == kernel_e::quadratic)
        weights = quadratic_bspline_weights<deriv_order>(localPos);
      else if constexpr (kt == kernel_e::cubic)
        weights = cubic_bspline_weights<deriv_order>(localPos);
      localPos *= dx;
    }

    constexpr auto range() const noexcept { return ndrange<dim>(width); }

  protected:
    template <typename... Tn, std::size_t... Is,
              enable_if_all<(sizeof...(Is) == dim), (sizeof...(Tn) == dim)> = 0>
    constexpr T weight_impl(const std::tuple<Tn...> &loc, index_seq<Is...>) const noexcept {
      value_type ret{1};
      ((void)(ret *= get<0>(weights)(Is, std::get<Is>(loc))), ...);
      return ret;
    }
    template <std::size_t I, typename... Tn, std::size_t... Is, auto ord = deriv_order,
              enable_if_all<(sizeof...(Is) == dim), (sizeof...(Tn) == dim), (ord > 0)> = 0>
    constexpr T weightGradient_impl(const std::tuple<Tn...> &loc, index_seq<Is...>) const noexcept {
      value_type ret{1};
      ((void)(ret *= (I == Is ? get<1>(weights)(Is, std::get<Is>(loc))
                              : get<0>(weights)(Is, std::get<Is>(loc)))),
       ...);
      return ret;
    }
    template <typename... Tn, std::size_t... Is, auto ord = deriv_order,
              enable_if_all<(sizeof...(Is) == dim), (sizeof...(Tn) == dim), (ord > 0)> = 0>
    constexpr TV weightGradients_impl(const std::tuple<Tn...> &loc,
                                      index_seq<Is...>) const noexcept {
      return TV{weightGradient_impl<Is>(loc, index_seq<Is...>{})...};
    }

  public:
    template <typename... Tn> constexpr IV offset(const std::tuple<Tn...> &loc) const noexcept {
      return make_vec<index_type>(loc);
    }

    template <typename... Tn,
              enable_if_all<(!is_std_tuple<Tn>() && ... && (sizeof...(Tn) == dim))> = 0>
    constexpr auto weight(Tn &&...is) const noexcept {
      return weight(std::forward_as_tuple(FWD(is)...));
    }

    template <typename... Tn> constexpr T weight(const std::tuple<Tn...> &loc) const noexcept {
      return weight_impl(loc, std::index_sequence_for<Tn...>{});
    }
    template <std::size_t I, typename... Tn, auto ord = deriv_order>
    constexpr std::enable_if_t<(ord > 0), T> weightGradient(
        const std::tuple<Tn...> &loc) const noexcept {
      return weightGradient_impl<I>(loc, std::index_sequence_for<Tn...>{});
    }

    template <typename... Tn, auto ord = deriv_order>
    constexpr std::enable_if_t<(ord > 0), TV> weightGradients(
        const std::tuple<Tn...> &loc) const noexcept {
      return weightGradients_impl(loc, std::index_sequence_for<Tn...>{});
    }
    template <typename... Tn> constexpr TV diff(const std::tuple<Tn...> &pos) const noexcept {
      return offset(pos) * dx - localPos;
    }
    template <typename... Tn> constexpr IV coord(const std::tuple<Tn...> &pos) const noexcept {
      return offset(pos) + corner;
    }

    TV localPos{TV::zeros()};
    WeightScratchPad weights{};
    IV corner{IV::zeros()};
    value_type dx{0};
  };

  template <grid_e gt = grid_e::collocated, kernel_e kt = kernel_e::quadratic, int deriv_order = 0,
            typename Ti = int, typename T = f32, int dim = 3, typename TT = T>
  constexpr LocalArena<gt, kt, deriv_order, T, dim, Ti> make_local_arena(TT dx,
                                                                         const vec<T, dim> &pos) {
    LocalArena<gt, kt, deriv_order, T, dim, Ti> ret{};
    ret.init(dx, pos);
    return ret;
  }

}  // namespace zs