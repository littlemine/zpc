#pragma once

#include <vector>

#include "zensim/ZpcResource.hpp"

namespace zs {

  template <typename T> using Shared = SharedPtr<T>;
  template <typename T> using Weak = WeakPtr<T>;
  template <typename T> using Unique = UniquePtr<T>;

}  // namespace zs