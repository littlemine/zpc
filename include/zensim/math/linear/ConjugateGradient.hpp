#pragma once
#include <cmath>
#include <iostream>

#include "LinearOperators.hpp"

namespace zs {

  /// Bow/Math/LinearSolver/ConjugateGradient.h
  template <typename T, int dim, typename Index = zs::size_t> struct ConjugateGradient {
    using TV = Vector<T>;
    using allocator_type = ZSPmrAllocator<>;
    using size_type = zs::make_unsigned_t<Index>;

    int maxIters;
    TV x_, r_, p_, q_, temp_;
    TV mr_, s_;
    // for dot
    TV dofSqr_;
    TV normSqr_;
    size_type numDofs;
    T tol;
    T relTol;

    ConjugateGradient(const allocator_type& allocator, size_type ndofs)
        : x_{allocator, ndofs},
          r_{allocator, ndofs},
          p_{allocator, ndofs},
          q_{allocator, ndofs},
          temp_{allocator, ndofs},
          mr_{allocator, ndofs},
          s_{allocator, ndofs},
          dofSqr_{allocator, ndofs},
          normSqr_{allocator, 1},
          numDofs{ndofs},
          tol{is_same_v<T, float> ? (T)1e-6 : (T)1e-12},
          maxIters{1000},
          relTol{0.5f} {}
    ConjugateGradient(memsrc_e mre = memsrc_e::host, ProcID devid = -1)
        : ConjugateGradient{get_memory_source(mre, devid), (size_type)0} {}
    ConjugateGradient(size_type count, memsrc_e mre = memsrc_e::host, ProcID devid = -1)
        : ConjugateGradient{get_memory_source(mre, devid), count} {}

    void resize(size_type ndofs) {
      numDofs = ndofs;
      x_.resize(ndofs);
      r_.resize(ndofs);
      p_.resize(ndofs);
      q_.resize(ndofs);
      temp_.resize(ndofs);
      mr_.resize(ndofs);
      s_.resize(ndofs);
      dofSqr_.resize(ndofs);
    }

    template <typename DV> void print(DV&& dv) {
      for (size_t i = 0; i != dv.size(); ++i) std::cout << dv.get(i) << ' ';
      std::cout << '\n';
    }

    template <class ExecutionPolicy, typename DofViewA, typename DofViewB>
    T dotProduct(ExecutionPolicy&& policy, DofViewA a, DofViewB b) {
      constexpr execspace_e space = RM_REF_T(policy)::exec_tag::value;
      using ValueT = typename std::iterator_traits<RM_CVREF_T(std::begin(a))>::value_type;
      auto dofSqr = dof_view<space, dim>(dofSqr_);
      DofCompwiseOp{multiplies<void>{}}(policy, a, b, dofSqr);
      reduce(policy, std::begin(dofSqr), std::end(dofSqr),
             std::begin(dof_view<space, dim>(normSqr_)), 0, plus<ValueT>{});
      return normSqr_.clone({memsrc_e::host, -1})[0];
    }

    template <class ExecutionPolicy, typename M, typename XView, typename BView>
    int solve(ExecutionPolicy&& policy, M&& A, XView&& xinout, BView&& b) {
      constexpr execspace_e space = RM_REF_T(policy)::exec_tag::value;
      resize(xinout.numEntries());

      auto x = dof_view<space, dim>(x_);
      policy(range(numDofs), DofAssign{xinout, x});

      auto r = dof_view<space, dim>(r_), p = dof_view<space, dim>(p_), q = dof_view<space, dim>(q_),
           temp = dof_view<space, dim>(temp_);
      auto mr = dof_view<space, dim>(mr_), s = dof_view<space, dim>(s_);
      auto dofSqr = dof_view<space, dim>(dofSqr_), normSqr = dof_view<space, dim>(normSqr_);

      int iter = 0;
      auto condition = [&iter]() { return iter >= 1; };
      auto shouldPrint = [](bool v = true) { return v; };
      auto checkVector = [&policy, this](auto&& v) {
        auto res = dotProduct(policy, v, v);
        std::cout << "\tchecking result dotprod: " << res << '\n';
      };
      T alpha, beta, residualPreconditionedNorm, zTrk, zTrkLast;

      checkVector(b);
      A.multiply(policy, x, temp);
      DofCompwiseOp{minus<void>{}}(policy, b, temp, r);  // r = b - temp;
      if (shouldPrint()) {
        std::cout << "pre loop, b - Ax -> r\n";
        checkVector(r);
      }
      A.project(policy, r);
      if (shouldPrint()) {
        std::cout << "pre loop, project r\n";
        checkVector(r);
      }

      A.precondition(policy, r, q);  // NOTE: requires that preconditioning matrix is projected
      if (shouldPrint()) {
        std::cout << "pre loop, Mr -> q\n";
        checkVector(q);
      }
      policy(range(numDofs), DofAssign{q, p});  // p = q;
      if (shouldPrint()) {
        std::cout << "pre loop, q -> p\n";
        checkVector(p);
      }

      zTrk = dotProduct(policy, r, q);  // zTrk = std::abs(dotProduct(r, q));
      std::cout << "pre loop, zTrk " << zTrk << " (r.dot(q))\n";
      residualPreconditionedNorm = std::sqrt(zTrk);
      T localTol = std::min(relTol * residualPreconditionedNorm, tol);
      for (; iter != maxIters; ++iter) {
        if (shouldPrint(condition()))
          std::cout << "iter: " << iter << ", norm: " << residualPreconditionedNorm
                    << ", tol " << localTol << '\n';
        if (residualPreconditionedNorm <= localTol) break;
        A.multiply(policy, p, temp);
        if (shouldPrint(condition())) {
          std::cout << "iter: " << iter << ", Ap -> temp\n";
          checkVector(temp);
        }
        A.project(policy, temp);
        if (shouldPrint(condition())) {
          std::cout << "iter: " << iter << ", project temp\n";
          checkVector(temp);
        }
        alpha = zTrk / dotProduct(policy, temp, p);  // alpha = zTrk / dotProduct(temp, p);
        if (shouldPrint(condition())) std::cout << "iter: " << iter << ", alpha " << alpha << '\n';

        DofCompwiseOp{LinearCombineOp(alpha)}(policy, p, x, x);  // x = x + alpha * p;
        if (shouldPrint(condition())) {
          std::cout << "iter: " << iter << ", x += a * p\n";
          checkVector(x);
        }
        DofCompwiseOp{LinearCombineOp(-alpha)}(policy, temp, r, r);  // r = r - alpha * temp;
        if (shouldPrint(condition())) {
          std::cout << "iter: " << iter << ", r -= a * temp\n";
          checkVector(r);
        }
        A.precondition(policy, r, q);  // NOTE: requires that preconditioning matrix is projected
        if (shouldPrint(condition())) {
          std::cout << "iter: " << iter << ", Mr -> q\n";
          checkVector(q);
        }

        zTrkLast = zTrk;
        zTrk = dotProduct(policy, q, r);  // zTrk = dotProduct(q, r);
        if (shouldPrint(condition()))
          std::cout << "iter: " << iter << ", ztrk(dot(q, r)) " << zTrkLast << " -> "
                    << zTrk << '\n';
        beta = zTrk / zTrkLast;
        if (shouldPrint(condition())) std::cout << "iter: " << iter << ", beta " << beta << '\n';

        DofCompwiseOp{LinearCombineOp(beta)}(policy, p, q, p);  // p = q + beta * p;
        if (shouldPrint(condition())) {
          std::cout << "iter: " << iter << ", p = q + beta * p\n";
          checkVector(p);
        }

        residualPreconditionedNorm = std::sqrt(zTrk);
        if (iter >= 1) getchar();
      }
      policy(range(numDofs), DofAssign{x, xinout});
      return iter;
    }
  };

}  // namespace zs