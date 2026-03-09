#include "utils/parallel_primitives.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "utils/initialization.hpp"
#include "zensim/execution/ExecutionPolicy.hpp"

int main() {
  using namespace zs;
  auto pol = seq_exec();
  auto reduction = [&pol](size_t n) {
    // Vector<int> vals = gen_rnd_ints(n, make_monoid(getmin<int>()).identity()
    auto vals = gen_rnd_tv_ints(n, make_monoid(getmin<int>()).identity());
    if (!test_reduction(pol, range(vals, "b"), getmax<int>()))
      throw std::runtime_error("getmax<int> failed");
    if (!test_reduction(pol, range(vals, "b"), getmin<int>()))
      throw std::runtime_error("getmin<int> failed");
    vals = gen_rnd_tv_ints(n, 100);
    if (!test_reduction(pol, range(vals, "b"), plus<int>()))
      throw std::runtime_error("plus<int> failed");
  };
  for (int i = 0; i != 10; ++i) {
    reduction(1);
    reduction(2);
    reduction(7);
    reduction(16);
    reduction(128);
    reduction(1024);
    reduction(2000000);
  }

#if ZS_ENABLE_OPENMP
  auto ompPol = omp_exec();
  auto test_float_radix_sort = [&ompPol](size_t n) {
    auto keys = gen_rnd_floats(n);
    for (size_t i = 0; i < n; ++i) {
      const float bucket = static_cast<float>((static_cast<int>(i % 29) - 14) * 7);
      keys[i] = bucket + keys[i] * 0.125f + static_cast<float>(i) * 1e-6f;
      if (i % 2) keys[i] = -keys[i];
    }

    Vector<float> sorted{n};
    std::vector<float> expected(n);
    for (size_t i = 0; i < n; ++i) expected[i] = keys[i];
    std::stable_sort(expected.begin(), expected.end());

    radix_sort(ompPol, keys.begin(), keys.end(), sorted.begin());
    for (size_t i = 0; i < n; ++i) {
      if (sorted[i] != expected[i]) {
        throw std::runtime_error("float radix_sort failed");
      }
    }
  };

  auto test_double_radix_sort = [&ompPol](size_t n) {
    Vector<double> keys{n};
    for (size_t i = 0; i < n; ++i) {
      const double bucket = static_cast<double>((static_cast<int>(i % 31) - 15) * 11);
      keys[i] = bucket + static_cast<double>(i % 7) * 0.03125 + static_cast<double>(i) * 1e-9;
      if (i % 2) keys[i] = -keys[i];
    }

    Vector<double> sorted{n};
    std::vector<double> expected(n);
    for (size_t i = 0; i < n; ++i) expected[i] = keys[i];
    std::stable_sort(expected.begin(), expected.end());

    radix_sort(ompPol, keys.begin(), keys.end(), sorted.begin());
    for (size_t i = 0; i < n; ++i) {
      if (sorted[i] != expected[i]) throw std::runtime_error("double radix_sort failed");
    }
  };

  auto test_float_radix_sort_pair = [&ompPol](size_t n) {
    Vector<float> keys{n};
    Vector<int> values{n};
    for (size_t i = 0; i < n; ++i) {
      const float base = static_cast<float>(static_cast<int>(i % 17) - 8);
      keys[i] = (i % 3 == 0) ? base : (base + 0.25f);
      if (i % 2) keys[i] = -keys[i];
      values[i] = static_cast<int>(i);
    }

    Vector<float> sortedKeys{n};
    Vector<int> sortedValues{n};
    std::vector<std::pair<float, int>> expected(n);
    for (size_t i = 0; i < n; ++i) expected[i] = {keys[i], values[i]};
    std::stable_sort(expected.begin(), expected.end(), [](const auto &lhs, const auto &rhs) {
      return lhs.first < rhs.first;
    });

    radix_sort_pair(ompPol, keys.begin(), values.begin(), sortedKeys.begin(), sortedValues.begin(),
                    static_cast<zs::size_t>(n));
    for (size_t i = 0; i < n; ++i) {
      if (sortedKeys[i] != expected[i].first || sortedValues[i] != expected[i].second) {
        throw std::runtime_error("float radix_sort_pair failed");
      }
    }
  };

  auto test_double_radix_sort_pair = [&ompPol](size_t n) {
    Vector<double> keys{n};
    Vector<int> values{n};
    for (size_t i = 0; i < n; ++i) {
      const double base = static_cast<double>(static_cast<int>(i % 19) - 9);
      keys[i] = (i % 4 == 0) ? base : (base + 0.125);
      if (i % 2) keys[i] = -keys[i];
      values[i] = static_cast<int>(i);
    }

    Vector<double> sortedKeys{n};
    Vector<int> sortedValues{n};
    std::vector<std::pair<double, int>> expected(n);
    for (size_t i = 0; i < n; ++i) expected[i] = {keys[i], values[i]};
    std::stable_sort(expected.begin(), expected.end(), [](const auto &lhs, const auto &rhs) {
      return lhs.first < rhs.first;
    });

    radix_sort_pair(ompPol, keys.begin(), values.begin(), sortedKeys.begin(), sortedValues.begin(),
                    static_cast<zs::size_t>(n));
    for (size_t i = 0; i < n; ++i) {
      if (sortedKeys[i] != expected[i].first || sortedValues[i] != expected[i].second)
        throw std::runtime_error("double radix_sort_pair failed");
    }
  };

  auto test_signed_zero_radix_sort_pair = [&ompPol]() {
    Vector<float> keys{8};
    Vector<int> values{8};

    keys[0] = -1.0f;
    keys[1] = -0.0f;
    keys[2] = 0.0f;
    keys[3] = 0.0f;
    keys[4] = -0.0f;
    keys[5] = 1.0f;
    keys[6] = -0.0f;
    keys[7] = 0.0f;

    for (int i = 0; i < 8; ++i) values[i] = i;

    Vector<float> sortedKeys{8};
    Vector<int> sortedValues{8};
    std::vector<std::pair<float, int>> expected(8);
    for (int i = 0; i < 8; ++i) expected[i] = {keys[i], values[i]};
    std::stable_sort(expected.begin(), expected.end(), [](const auto &lhs, const auto &rhs) {
      return lhs.first < rhs.first;
    });

    radix_sort_pair(ompPol, keys.begin(), values.begin(), sortedKeys.begin(), sortedValues.begin(),
                    static_cast<zs::size_t>(8));
    for (int i = 0; i < 8; ++i) {
      if (sortedKeys[i] != expected[i].first || sortedValues[i] != expected[i].second)
        throw std::runtime_error("signed zero radix_sort_pair failed");
    }
  };

  for (int i = 0; i != 4; ++i) {
    test_float_radix_sort(257);
    test_float_radix_sort(4096);
    test_double_radix_sort(257);
    test_double_radix_sort(4096);
    test_float_radix_sort_pair(257);
    test_float_radix_sort_pair(4096);
    test_double_radix_sort_pair(257);
    test_double_radix_sort_pair(4096);
  }
  test_signed_zero_radix_sort_pair();
#endif

  return 0;
}