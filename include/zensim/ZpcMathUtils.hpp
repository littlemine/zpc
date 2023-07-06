#pragma once
#include "zensim/ZensimExport.hpp"
#include "zensim/ZpcFunctional.hpp"
#include "zensim/types/Property.h"

#if defined(__CUDACC__)

#else
extern "C" {
// stdio.h
int printf(const char *format, ...);

#  if defined(__linux__)
// stdlib.h
int abs(int) noexcept;
long long llabs(long long) noexcept;
// math.h
float copysignf(float x, float y) noexcept;
double copysign(double x, double y) noexcept;
float fabsf(float) noexcept;
double fabs(double) noexcept;
float fmaxf(float x, float y) noexcept;
double fmax(double x, double y) noexcept;
float fminf(float x, float y) noexcept;
double fmin(double x, double y) noexcept;
float fmaf(float, float, float) noexcept;
double fma(double x, double y, double z) noexcept;
float fmodf(float, float) noexcept;
double fmod(double, double) noexcept;
float ceilf(float) noexcept;
double ceil(double) noexcept;
float floorf(float) noexcept;
double floor(double) noexcept;
float sqrtf(float) noexcept;
double sqrt(double) noexcept;
float logf(float) noexcept;
double log(double) noexcept;
float log1pf(float) noexcept;
double log1p(double) noexcept;
float expf(float) noexcept;
double exp(double) noexcept;
float powf(float, float) noexcept;
double pow(double, double) noexcept;

float sinhf(float) noexcept;
double sinh(double) noexcept;
float sinf(float) noexcept;
double sin(double) noexcept;
float asinhf(float) noexcept;
double asinh(double) noexcept;
float asinf(float) noexcept;
double asin(double) noexcept;

float cosf(float) noexcept;
double cos(double) noexcept;
float coshf(float) noexcept;
double cosh(double) noexcept;
float acoshf(float) noexcept;
double acosh(double) noexcept;
float acosf(float) noexcept;
double acos(double) noexcept;

float atan2f(float, float) noexcept;
double atan2(double, double) noexcept;

float modff(float arg, float *iptr) noexcept;
double modf(double arg, double *iptr) noexcept;
float frexpf(float arg, int *exp) noexcept;
double frexp(double arg, int *exp) noexcept;
float ldexpf(float arg, int exp) noexcept;
double ldexp(double arg, int exp) noexcept;

// not yet wrapped
float log2f(float) noexcept;
double log2(double) noexcept;
float log10f(float) noexcept;
double log10(double) noexcept;
float roundf(float) noexcept;
double round(double) noexcept;
float truncf(float) noexcept;
double trunc(double) noexcept;
float atanf(float) noexcept;
double atan(double) noexcept;
float tanf(float) noexcept;
double tan(double) noexcept;
float tanhf(float) noexcept;
double tanh(double) noexcept;
// float rintf(float);
// double rint(double);

#  elif defined(_WIN64)
// stdlib.h
int abs(int);
long labs(long);
long long llabs(long long);
// math.h (ucrt/corecrt_math.h)
_ACRTIMP float copysignf(float x, float y);
_ACRTIMP double copysign(double x, double y);
float fabsf(float);
double fabs(double);
_ACRTIMP float fmaxf(float x, float y);
_ACRTIMP double fmax(double x, double y);
_ACRTIMP float fminf(float x, float y);
_ACRTIMP double fmin(double x, double y);
_ACRTIMP float fmaf(float, float, float);
_ACRTIMP double fma(double x, double y, double z);
_ACRTIMP float fmodf(float, float);
double fmod(double, double);
_ACRTIMP float ceilf(float);
_ACRTIMP double ceil(double);
_ACRTIMP float floorf(float);
_ACRTIMP double floor(double);
_ACRTIMP float sqrtf(float);
double sqrt(double);
_ACRTIMP float logf(float);
double log(double);
_ACRTIMP float log1pf(float);
_ACRTIMP double log1p(double);
_ACRTIMP float expf(float);
double exp(double);
_ACRTIMP float powf(float, float);
double pow(double, double);

_ACRTIMP float sinhf(float);
double sinh(double);
_ACRTIMP float sinf(float);
double sin(double);
_ACRTIMP float asinhf(float);
_ACRTIMP double asinh(double);
_ACRTIMP float asinf(float);
double asin(double);

_ACRTIMP float cosf(float);
double cos(double);
_ACRTIMP float coshf(float);
double cosh(double);
_ACRTIMP float acoshf(float);
_ACRTIMP double acosh(double);
_ACRTIMP float acosf(float);
double acos(double);

_ACRTIMP float atan2f(float, float);
double atan2(double, double);

_ACRTIMP float modff(float arg, float *iptr);
_ACRTIMP double modf(double arg, double *iptr);
float frexpf(float arg, int *exp);
_ACRTIMP double frexp(double arg, int *exp);
float ldexpf(float arg, int exp);
_ACRTIMP double ldexp(double arg, int exp);

// not yet wrapped
_ACRTIMP float log2f(float);
_ACRTIMP double log2(double);
_ACRTIMP float log10f(float);
_ACRTIMP double log10(double);
_ACRTIMP float roundf(float);
_ACRTIMP double round(double);
_ACRTIMP float truncf(float);
_ACRTIMP double trunc(double);
_ACRTIMP float atanf(float);
_ACRTIMP double atan(double);
_ACRTIMP float tanf(float);
_ACRTIMP double tan(double);
_ACRTIMP float tanhf(float);
_ACRTIMP double tanh(double);
// float rintf(float);
// double rint(double);

#  endif
}
#endif

namespace zs {

  namespace mathutil_impl {
    // constexpr scan only available in c++20:
    // https://en.cppreference.com/w/cpp/algorithm/exclusive_scan
    template <typename... Args, auto... Is>
    constexpr auto incl_prefix_sum_impl(sint_t I, index_sequence<Is...>, Args &&...args) noexcept {
      return (((sint_t)Is <= I ? forward<Args>(args) : 0) + ...);
    }
    template <typename... Args, auto... Is>
    constexpr auto excl_prefix_sum_impl(sint_t I, index_sequence<Is...>, Args &&...args) noexcept {
      return (((sint_t)Is < I ? forward<Args>(args) : 0) + ...);
    }
    template <typename... Args, auto... Is>
    constexpr auto excl_suffix_mul_impl(sint_t I, index_sequence<Is...>, Args &&...args) noexcept {
      return (((sint_t)Is > I ? forward<Args>(args) : 1) * ...);
    }
  }  // namespace mathutil_impl

  /// copied from gcem_options.hpp
  constexpr double g_pi = 3.1415926535897932384626433832795028841972L;
  constexpr double g_half_pi = 1.5707963267948966192313216916397514420986L;
  constexpr double g_sqrt2 = 1.4142135623730950488016887242096980785697L;

  namespace math {

    template <typename T, enable_if_t<is_floating_point_v<T>> = 0>
    constexpr bool near_zero(T v) noexcept {
      constexpr auto eps = (T)128 * zs::detail::deduce_numeric_epsilon<T>();
      return v >= -eps && v <= eps;
    }

    template <typename T, enable_if_t<is_fundamental_v<T>> = 0> constexpr T min(T x, T y) noexcept {
      return y < x ? y : x;
    }
    template <typename T, enable_if_t<is_fundamental_v<T>> = 0> constexpr T max(T x, T y) noexcept {
      return y > x ? y : x;
    }
    template <typename T, enable_if_t<is_fundamental_v<T>> = 0> constexpr T abs(T x) noexcept {
      return x < 0 ? -x : x;
    }
    template <typename T, enable_if_t<is_fundamental_v<T>> = 0>
    constexpr const T &clamp(const T &x, const T &a, const T &b) noexcept {
      return x < a ? a : (b < x ? b : x);
    }
    // TODO refer to:
    // https://github.com/mountunion/ModGCD-OneGPU/blob/master/ModGCD-OneGPU.pdf
    // http://www.iaeng.org/IJCS/issues_v42/issue_4/IJCS_42_4_01.pdf
    template <typename Ti, enable_if_t<is_integral_v<Ti>> = 0>
    constexpr Ti gcd(Ti u, Ti v) noexcept {
      while (v != 0) {
        auto r = u % v;
        u = v;
        v = r;
      }
      return u;
    }
    template <typename Ti, enable_if_t<is_integral_v<Ti>> = 0>
    constexpr Ti lcm(Ti u, Ti v) noexcept {
      return (u / gcd(u, v)) * v;
    }

    /// binary_op_result
    template <typename T0, typename T1> struct binary_op_result {
      template <typename A = T0, typename B = T1,
                enable_if_all<is_integral_v<A>, is_integral_v<B>> = 0>
      static auto determine_type() -> conditional_t<
          is_signed_v<A> && is_signed_v<B>, conditional_t<(sizeof(A) >= sizeof(B)), A, B>,
          conditional_t<
              is_signed_v<A>, A,
              conditional_t<is_signed_v<B>, B, conditional_t<(sizeof(A) >= sizeof(B)), A, B>>>>;
      template <typename A = T0, typename B = T1,
                enable_if_t<!is_integral_v<A> || !is_integral_v<B>> = 0>
      static auto determine_type() -> common_type_t<A, B>;
      using type = decltype(determine_type());
    };
    template <typename T0, typename T1> using binary_op_result_t =
        typename binary_op_result<T0, T1>::type;

    /// op_result
    template <typename... Ts> struct op_result;
    template <typename T> struct op_result<T> {
      using type = T;
    };
    template <typename T, typename... Ts> struct op_result<T, Ts...> {
      using type = binary_op_result_t<T, typename op_result<Ts...>::type>;
    };
    /// @brief determine the most appropriate resulting type of a binary operation
    template <typename... Args> using op_result_t = typename op_result<Args...>::type;

    namespace detail {
      template <typename T, typename Tn, enable_if_t<is_signed_v<Tn>> = 0>
      constexpr T pow_integral_recursive(T base, T val, Tn exp) noexcept {
        return exp > (Tn)1
                   ? ((exp & (Tn)1) ? pow_integral_recursive(base * base, val * base, exp / (Tn)2)
                                    : pow_integral_recursive(base * base, val, exp / (Tn)2))
                   : (exp == (Tn)1 ? val * base : val);
      }
    }  // namespace detail
    template <typename T, typename Tn, enable_if_all<is_arithmetic_v<T>, is_signed_v<Tn>> = 0>
    constexpr auto pow_integral(T base, Tn exp) noexcept {
      using R = T;  // math::op_result_t<T0, T1>;
      return exp == (Tn)3
                 ? base * base * base
                 : (exp == (Tn)2
                        ? base * base
                        : (exp == (Tn)1
                               ? base
                               : (exp == (Tn)0 ? (R)1
                                               : (exp == zs::detail::deduce_numeric_max<Tn>()
                                                      ? zs::detail::deduce_numeric_infinity<R>()
                                                      // make signed to get rid of compiler warn
                                                      : (exp < 0 ? (R)0
                                                                 : detail::pow_integral_recursive(
                                                                     (R)base, (R)1, (Tn)exp))))));
    }

  }  // namespace math

  /**
   *  math intrinsics (not constexpr at all! just cheating the compiler)
   */
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T copysign(T mag, T sgn, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::copysignf(mag, sgn);
      else
        return ::copysign((double)mag, (double)sgn);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [copysign] is missing!");
      return 0;
#endif
    } else {
#if !defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::copysignf(mag, sgn);
      else
        return ::copysign((double)mag, (double)sgn);
#endif
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T abs(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fabsf(v);
      else
        return ::fabs((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [abs] is missing!");
      return 0;
#endif
    } else {
#if !defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fabsf(v);
      else
        return ::fabs((double)v);
#endif
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T max(T x, T y, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if ZS_ENABLE_CUDA && defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fmaxf(x, y);
      else
        return ::fmax((double)x, (double)y);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [max] is missing!");
      return 0;
#endif
    } else {
#if !defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fmaxf(x, y);
      else
        return ::fmax((double)x, (double)y);
#endif
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T min(T x, T y, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fminf(x, y);
      else
        return ::fmin((double)x, (double)y);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [min] is missing!");
      return 0;
#endif
    } else {
#if !defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fminf(x, y);
      else
        return ::fmin((double)x, (double)y);
#endif
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T fma(T x, T y, T z, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fmaf(x, y, z);
      else
        return ::fma((double)x, (double)y, (double)z);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [fma] is missing!");
      return 0;
#endif
    } else {
#if !defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fmaf(x, y, z);
      else
        return ::fma((double)x, (double)y, (double)z);
#endif
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T fmod(T x, T y, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fmodf(x, y);
      else
        return ::fmod((double)x, (double)y);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [fmod] is missing!");
      return 0;
#endif
    } else {
#if !defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::fmodf(x, y);
      else
        return ::fmod((double)x, (double)y);
#endif
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T ceil(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::ceilf(v);
      else
        return ::ceil((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [ceil] is missing!");
      return 0;
#endif
    } else {
#if !defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::ceilf(v);
      else
        return ::ceil((double)v);
#endif
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T floor(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::floorf(v);
      else
        return ::floor((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [floor] is missing!");
      return 0;
#endif
    } else {
#if !defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::floorf(v);
      else
        return ::floor((double)v);
#endif
    }
  }

  // different from math::sqrt
  template <typename T, enable_if_t<is_arithmetic_v<T>> = 0> constexpr T sqr(T v) noexcept {
    return v * v;
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T sqrt(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::sqrtf(v);
      else
        return ::sqrt((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [sqrt] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::sqrtf(v);
      else
        return ::sqrt((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T rsqrt(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::rsqrtf(v);
      else
        return ::rsqrt((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [rsqrt] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return (T)1 / (T)::sqrtf(v);
      else
        return (T)1 / (T)::sqrt((double)v);
    }
  }

  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T log(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::logf(v);
      else
        return ::log((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [log] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::logf(v);
      else
        return ::log((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T log1p(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::log1pf(v);
      else
        return ::log1p((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [log1p] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::log1pf(v);
      else
        return ::log1p((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T exp(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::expf(v);
      else
        return ::exp((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [exp] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::expf(v);
      else
        return ::exp((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T pow(T base, T exp, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::powf(base, exp);
      else
        return ::pow((double)base, (double)exp);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [pow] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::powf(base, exp);
      else
        return ::pow((double)base, (double)exp);
    }
  }

  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  ZS_FUNCTION T add_ru(T x, T y, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::__fadd_ru(x, y);
      else
        return ::__dadd_ru((double)x, (double)y);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [add_ru] is missing!");
      return 0;
#endif
    } else
      /// @note refer to https://en.cppreference.com/w/cpp/numeric/fenv/FE_round
      return (x + y);
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  ZS_FUNCTION T sub_ru(T x, T y, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::__fsub_ru(x, y);
      else
        return ::__dsub_ru((double)x, (double)y);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [sub_ru] is missing!");
      return 0;
#endif
    } else
      /// @note refer to https://en.cppreference.com/w/cpp/numeric/fenv/FE_round
      return (x - y);
  }

  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T sinh(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::sinhf(v);
      else
        return ::sinh((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [sinh] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::sinhf(v);
      else
        return ::sinh((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T sin(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::sinf(v);
      else
        return ::sin((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [sin] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::sinf(v);
      else
        return ::sin((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T asinh(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::asinhf(v);
      else
        return ::asinh((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [asinh] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::asinhf(v);
      else
        return ::asinh((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T asin(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::asinf(v);
      else
        return ::asin((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [asin] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::asinf(v);
      else
        return ::asin((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T cosh(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::coshf(v);
      else
        return ::cosh((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [cosh] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::coshf(v);
      else
        return ::cosh((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T cos(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::cosf(v);
      else
        return ::cos((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [cos] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::cosf(v);
      else
        return ::cos((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T acosh(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::acoshf(v);
      else
        return ::acosh((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [acosh] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::acoshf(v);
      else
        return ::acosh((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T acos(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::acosf(v);
      else
        return ::acos((double)v);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [acos] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::acosf(v);
      else
        return ::acos((double)v);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T atan2(T y, T x, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::atan2f(y, x);
      else
        return ::atan2((double)y, (double)x);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [atan2] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::atan2f(y, x);
      else
        return ::atan2((double)y, (double)x);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr bool isnan(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      return ::isnan(v) != 0;  // due to msvc
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [isnan] is missing!");
      return 0;
#endif
    } else
      // https://en.cppreference.com/w/c/numeric/math/isnan
      return v != v;
  }
#if 0
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr bool isinf(T v, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#  if defined(__CUDACC__)
      return ::isinf(v) != 0;  // due to msvc
#  else
      static_assert(space != execspace_e::cuda, "cuda implementation of [isinf] is missing!");
      return 0;
#  endif
    } else
      return std::isinf(v);
  }
#endif

  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T modf(T x, T *iptr, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      static_assert(is_same_v<T, float> || is_same_v<T, double>, "modf only supports float/double");
      if constexpr (is_same_v<T, float>)
        return ::modff(x, iptr);
      else if constexpr (is_same_v<T, double>)
        return ::modf(x, iptr);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [modf] is missing!");
      return 0;
#endif
    } else {
      static_assert(is_same_v<T, float> || is_same_v<T, double>, "modf only supports float/double");
      if constexpr (is_same_v<T, float>)
        return ::modff(x, iptr);
      else if constexpr (is_same_v<T, double>)
        return ::modf(x, iptr);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T frexp(T x, int *exp, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::frexpf(x, exp);
      else
        return ::frexp((double)x, exp);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [frexp] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::frexpf(x, exp);
      else
        return ::frexp((double)x, exp);
    }
  }
  template <typename T, execspace_e space = deduce_execution_space(),
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr T ldexp(T x, int exp, wrapv<space> = {}) noexcept {
    if constexpr (space == execspace_e::cuda) {
#if defined(__CUDACC__)
      if constexpr (is_same_v<T, float>)
        return ::ldexpf(x, exp);  // scalbnf(x, exp)
      else
        return ::ldexp((double)x, exp);
#else
      static_assert(space != execspace_e::cuda, "cuda implementation of [ldexp] is missing!");
      return 0;
#endif
    } else {
      if constexpr (is_same_v<T, float>)
        return ::ldexpf(x, exp);  // scalbnf(x, exp)
      else
        return ::ldexp((double)x, exp);
    }
  }

  namespace math {

    /**
     * Robustly computing log(x+1)/x
     */
    template <typename T, execspace_e space = deduce_execution_space(),
              enable_if_t<is_floating_point_v<T>> = 0>
    constexpr T log_1px_over_x(const T x,
                               const T eps = zs::detail::deduce_numeric_epsilon<T>() * 10,
                               wrapv<space> = {}) noexcept {
      if (abs(x) < eps)
        return (T)1 - x / (T)2 + x * x / (T)3 - x * x * x / (T)4;
      else {
        return log1p(x, wrapv<space>{}) / x;
      }
    }
    /**
     * Robustly computing (logx-logy)/(x-y)
     */
    template <typename T, execspace_e space = deduce_execution_space(),
              enable_if_t<is_floating_point_v<T>> = 0>
    constexpr T diff_log_over_diff(const T x, const T y,
                                   const T eps = zs::detail::deduce_numeric_epsilon<T>() * 10,
                                   wrapv<space> = {}) noexcept {
      return log_1px_over_x(x / y - (T)1, eps, wrapv<space>{}) / y;
    }
    /**
     * Robustly computing (x logy- y logx)/(x-y)
     */
    template <typename T, execspace_e space = deduce_execution_space(),
              enable_if_t<is_floating_point_v<T>> = 0>
    constexpr T diff_interlock_log_over_diff(const T x, const T y, const T logy,
                                             const T eps
                                             = zs::detail::deduce_numeric_epsilon<T>() * 10,
                                             wrapv<space> = {}) noexcept {
      return logy - y * diff_log_over_diff(x, y, eps, wrapv<space>{});
    }

  }  // namespace math

  ///
  template <typename T, typename Data, enable_if_t<is_floating_point_v<T>> = 0>
  constexpr auto linear_interop(T alpha, Data &&a, Data &&b) noexcept {
    return a + (b - a) * alpha;
  }

  template <typename... Args> constexpr auto incl_prefix_sum(size_t I, Args &&...args) noexcept {
    return mathutil_impl::incl_prefix_sum_impl(I, index_sequence_for<Args...>{},
                                               forward<Args>(args)...);
  }
  template <typename... Args> constexpr auto excl_prefix_sum(size_t I, Args &&...args) noexcept {
    return mathutil_impl::excl_prefix_sum_impl(I, index_sequence_for<Args...>{},
                                               forward<Args>(args)...);
  }
  template <typename... Args> constexpr auto excl_suffix_mul(size_t I, Args &&...args) noexcept {
    return mathutil_impl::excl_suffix_mul_impl(I, index_sequence_for<Args...>{},
                                               forward<Args>(args)...);
  }
  template <typename Tn, Tn... Ns>
  constexpr auto incl_prefix_sum(size_t I, integer_sequence<Tn, Ns...>) noexcept {
    return incl_prefix_sum(I, Ns...);
  }
  template <typename Tn, Tn... Ns>
  constexpr auto excl_prefix_sum(size_t I, integer_sequence<Tn, Ns...>) noexcept {
    return excl_prefix_sum(I, Ns...);
  }
  template <typename Tn, Tn... Ns>
  constexpr auto excl_suffix_mul(size_t I, integer_sequence<Tn, Ns...>) noexcept {
    return excl_suffix_mul(I, Ns...);
  }

  template <typename T, enable_if_t<is_integral_v<T>> = 0>
  constexpr auto lower_trunc(const T v) noexcept {
    return v;
  }
  template <typename T, typename Ti = conditional_t<sizeof(T) <= sizeof(f32), i32, i64>,
            enable_if_t<is_floating_point_v<T>> = 0>
  constexpr auto lower_trunc(const T v, wrapt<Ti> = {}) noexcept {
    return static_cast<Ti>(zs::floor(v));
  }

}  // namespace zs