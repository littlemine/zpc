#include "utils/parallel_primitives.hpp"

#include "utils/initialization.hpp"
#include "zensim/execution/ExecutionPolicy.hpp"

int main() {
  using namespace zs;
  auto pol = seq_exec();
  // auto pol = omp_exec();
  auto reduction = [&pol](size_t n) {
    Vector<int> vals = gen_rnd_ints(n, make_monoid(getmin<int>()).e);
    if (!test_reduction(pol, vals, getmax<int>())) throw std::runtime_error("getmax<int> failed");
    if (!test_reduction(pol, vals, getmin<int>())) throw std::runtime_error("getmin<int> failed");
    vals = gen_rnd_ints(n, 100);
    if (!test_reduction(pol, vals, plus<int>())) throw std::runtime_error("plus<int> failed");
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
  return 0;
}