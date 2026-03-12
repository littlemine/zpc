#include "HashTable.hpp"

namespace zs {

#define INSTANTIATE_HASHTABLE(CoordIndexType, IndexType)                                \
  ZPC_INSTANTIATE_STRUCT HashTable<CoordIndexType, 1, IndexType, ZSPmrAllocator<>>;     \
  ZPC_INSTANTIATE_STRUCT HashTable<CoordIndexType, 2, IndexType, ZSPmrAllocator<>>;     \
  ZPC_INSTANTIATE_STRUCT HashTable<CoordIndexType, 3, IndexType, ZSPmrAllocator<>>;     \
  ZPC_INSTANTIATE_STRUCT HashTable<CoordIndexType, 4, IndexType, ZSPmrAllocator<>>;     \
  ZPC_INSTANTIATE_STRUCT HashTable<CoordIndexType, 1, IndexType, ZSPmrAllocator<true>>; \
  ZPC_INSTANTIATE_STRUCT HashTable<CoordIndexType, 2, IndexType, ZSPmrAllocator<true>>; \
  ZPC_INSTANTIATE_STRUCT HashTable<CoordIndexType, 3, IndexType, ZSPmrAllocator<true>>; \
  ZPC_INSTANTIATE_STRUCT HashTable<CoordIndexType, 4, IndexType, ZSPmrAllocator<true>>;

  INSTANTIATE_HASHTABLE(i32, i32)
  INSTANTIATE_HASHTABLE(i32, i64)

  // Explicit definitions for constexpr static members (MSVC dllexport workaround)
  template <> constexpr i32 HashTable<i32, 1, i32, ZSPmrAllocator<>>::key_scalar_sentinel_v;
  template <> constexpr i32 HashTable<i32, 2, i32, ZSPmrAllocator<>>::key_scalar_sentinel_v;
  template <> constexpr i32 HashTable<i32, 3, i32, ZSPmrAllocator<>>::key_scalar_sentinel_v;
  template <> constexpr i32 HashTable<i32, 4, i32, ZSPmrAllocator<>>::key_scalar_sentinel_v;
  // INSTANTIATE_HASHTABLE(i64, i64)

}  // namespace zs