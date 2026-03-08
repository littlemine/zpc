#include <cassert>

#include "zensim/ZpcResource.hpp"

namespace {

  struct Counted {
    static int alive;
    int value{0};

    explicit Counted(int v = 0) : value{v} { ++alive; }
    Counted(const Counted &o) : value{o.value} { ++alive; }
    ~Counted() { --alive; }
  };

  int Counted::alive = 0;

  struct Derived : Counted {
    explicit Derived(int v = 0) : Counted{v} {}
  };

}  // namespace

int main() {
  using namespace zs;

  {
    auto ptr = make_unique<Counted>(7);
    assert(ptr);
    assert(ptr->value == 7);
    assert(Counted::alive == 1);

    auto moved = zs::move(ptr);
    assert(!ptr);
    assert(moved);
    assert(moved->value == 7);
  }
  assert(Counted::alive == 0);

  {
    auto arr = make_unique<int[]>(4);
    arr[0] = 3;
    arr[3] = 9;
    assert(arr[0] == 3);
    assert(arr[3] == 9);
  }

  {
    auto shared = make_shared<Counted>(11);
    assert(shared);
    assert(shared->value == 11);
    assert(shared.use_count() == 1);
    assert(shared.unique());
    assert(Counted::alive == 1);

    SharedPtr<Counted> shared2 = shared;
    assert(shared.use_count() == 2);
    assert(shared2.use_count() == 2);

    WeakPtr<Counted> weak = shared;
    assert(!weak.expired());
    assert(weak.use_count() == 2);

    auto locked = weak.lock();
    assert(locked);
    assert(locked->value == 11);
    assert(locked.use_count() == 3);

    locked.reset();
    shared2.reset();
    assert(shared.use_count() == 1);
    assert(!weak.expired());

    shared.reset();
    assert(weak.expired());
    assert(!weak.lock());
  }
  assert(Counted::alive == 0);

  {
    SharedPtr<Derived> derived = make_shared<Derived>(19);
    SharedPtr<Counted> base = derived;
    SharedPtr<void> opaque = base;
    assert(derived.use_count() == 3);
    assert(base->value == 19);
    assert(opaque.get() == static_cast<void *>(base.get()));

    WeakPtr<Counted> weakBase = base;
    derived.reset();
    base.reset();
    assert(!weakBase.expired());
    auto relocked = weakBase.lock();
    assert(relocked);
    assert(relocked->value == 19);
    relocked.reset();
    assert(weakBase.expired());
  }
  assert(Counted::alive == 0);

  {
    auto sharedArr = make_shared<int[]>(3);
    sharedArr[0] = 1;
    sharedArr[1] = 2;
    sharedArr[2] = 3;
    SharedPtr<int[]> sharedArr2 = sharedArr;
    assert(sharedArr.use_count() == 2);
    assert(sharedArr2[1] == 2);
  }

  return 0;
}