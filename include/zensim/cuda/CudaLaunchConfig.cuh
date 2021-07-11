#pragma once
#include <driver_types.h>

#include <string>

namespace zs {

  struct LaunchConfig {
    LaunchConfig() = default;
    template <typename IndexType0, typename IndexType1> LaunchConfig(IndexType0 gs, IndexType1 bs)
        : dg{static_cast<unsigned int>(gs)},
          db{static_cast<unsigned int>(bs)},
          shmem{0},
          sid{cudaStreamDefault},
          autoConfig{false} {}
    template <typename IndexType0, typename IndexType1, typename IndexType2>
    LaunchConfig(IndexType0 gs, IndexType1 bs, IndexType2 mem)
        : dg{static_cast<unsigned int>(gs)},
          db{static_cast<unsigned int>(bs)},
          shmem{static_cast<unsigned int>(mem)},
          sid{cudaStreamDefault},
          autoConfig{false} {}
    template <typename IndexType0, typename IndexType1, typename IndexType2>
    LaunchConfig(IndexType0 gs, IndexType1 bs, IndexType2 mem, cudaStream_t stream)
        : dg{static_cast<unsigned int>(gs)},
          db{static_cast<unsigned int>(bs)},
          shmem{static_cast<unsigned int>(mem)},
          sid{stream},
          autoConfig{false} {}

    template <typename IndexType0> LaunchConfig(std::true_type, IndexType0 nwork)
        : dg{},
          db{static_cast<unsigned int>(nwork)},
          shmem{0},
          sid{cudaStreamDefault},
          autoConfig{true} {}
    template <typename IndexType0, typename IndexType1>
    LaunchConfig(std::true_type, IndexType0 nwork, IndexType1 mem)
        : dg{},
          db{static_cast<unsigned int>(nwork)},
          shmem{static_cast<unsigned int>(mem)},
          sid{cudaStreamDefault},
          autoConfig{true} {}
    template <typename IndexType0, typename IndexType1>
    LaunchConfig(std::true_type, IndexType0 nwork, IndexType1 mem, cudaStream_t stream)
        : dg{},
          db{static_cast<unsigned int>(nwork)},
          shmem{static_cast<unsigned int>(mem)},
          sid{stream},
          autoConfig{true} {}

    constexpr bool valid() const noexcept {
      if (autoConfig)
        return db.x > 0;
      else
        return dg.x && dg.y && dg.z && db.x && db.y && db.z;
    }
    constexpr bool enableAutoConfig() const noexcept { return autoConfig; }
    dim3 dg{};
    dim3 db{};
    unsigned int shmem{0};
    cudaStream_t sid{cudaStreamDefault};
    bool autoConfig{false};
  };

}  // namespace zs
