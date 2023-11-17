#pragma once

#include "zensim/ZpcIntrinsics.hpp"
#include "zensim/ZpcMeta.hpp"
#if defined(__CUDACC__)
#  ifdef ZPC_JIT_MODE
#  else
#    include <stdio.h>
#  endif
#endif

namespace zs {

  /// @brief null-terminated string
  /// @note 4 * 8 - 1 = 31 bytes (chars)
  template <typename CharT = char, zs::size_t NBytes = 4 * sizeof(void *)> struct BasicSmallString {
    using char_type = CharT;
    using size_type = size_t;
    static constexpr auto nbytes = NBytes;

    constexpr BasicSmallString() noexcept : buf{} {
      for (auto &c : buf) c = '\0';
    }
    constexpr BasicSmallString(const char tmp[]) : buf{} {
      size_type i = 0;
      for (; i + (size_type)1 != nbytes && tmp[i]; ++i) buf[i] = tmp[i];
      buf[i] = '\0';
#if ZS_ENABLE_OFB_ACCESS_CHECK
      if (i == nbytes - 1 && tmp[i]) {
        printf("the str [%s]\' size exceeds smallstring maximum length [%d]!\n", tmp, (int)nbytes);
      }
#endif
    }
    template <
        typename StrT,
        enable_if_t<is_assignable_v<
                        char_type &,
                        decltype(declval<StrT>()
                                     [0])> && is_integral_v<decltype(declval<StrT>().size())>> = 0>
    BasicSmallString(const StrT &str) noexcept {
      size_type n = str.size() < nbytes ? str.size() : nbytes - 1;
      buf[n] = '\0';
      for (size_type i = 0; i != n; ++i) buf[i] = str[i];
#if ZS_ENABLE_OFB_ACCESS_CHECK
      if (str.size() >= nbytes) {
        printf("the str [%s]\' size exceeds smallstring maximum length [%d]!\n", str.c_str(),
               (int)nbytes);
      }
#endif
    }
    constexpr BasicSmallString(const BasicSmallString &) noexcept = default;
    constexpr BasicSmallString &operator=(const BasicSmallString &) noexcept = default;
    constexpr BasicSmallString(BasicSmallString &&) noexcept = default;
    constexpr BasicSmallString &operator=(BasicSmallString &&) noexcept = default;

    constexpr decltype(auto) operator[](size_type i) const noexcept { return buf[i]; }
    constexpr decltype(auto) operator[](size_type i) noexcept { return buf[i]; }
    constexpr bool operator==(const char str[]) const noexcept {
      size_type i = 0, sz = size();
      for (; i != sz && str[i]; ++i)
        if (buf[i] != str[i]) return false;
      if (!(buf[i] || str[i])) return true;
      return false;
    }

    constexpr const char *asChars() const noexcept { return buf; }
    constexpr operator const char *() const noexcept { return buf; }
    constexpr size_type size() const noexcept {
      size_type i = 0;
      for (; buf[i]; ++i)
        ;
      return i;
    }
    friend constexpr BasicSmallString operator+(const BasicSmallString &a,
                                                const BasicSmallString &b) noexcept {
      BasicSmallString ret{};
      size_type i = 0;
      for (; i + (size_type)1 != nbytes && a.buf[i]; ++i) ret.buf[i] = a.buf[i];
      for (size_type j = 0; i + (size_type)1 != nbytes && b.buf[j]; ++i, ++j) ret.buf[i] = b.buf[j];
      ret.buf[i] = '\0';
#if ZS_ENABLE_OFB_ACCESS_CHECK
      if (a.size() + b.size() >= nbytes) {
        printf("concatenating str [%s] and str [%s] exceeds smallstring maximum length [%d]!\n",
               a.asChars(), b.asChars(), (int)nbytes);
      }
#endif
      return ret;
    }

    char_type buf[nbytes];
  };
  using SmallString = BasicSmallString<>;

  /// property tag
  struct PropertyTag {
    SmallString name;
    int numChannels;
  };

}  // namespace zs