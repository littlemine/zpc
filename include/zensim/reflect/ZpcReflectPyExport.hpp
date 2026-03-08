// Copyright (c) zpc contributors. Licensed under the MIT License.
// ZpcReflectPyExport.hpp — C-ABI helpers for exposing reflected types to Python
// via ctypes.
//
// The code generator emits a `.reflected_py.cpp` per target that includes
// `extern "C" ZPC_EXPORT` functions for every reflected type:
//
//   - zs_reflect_<Type>_create()             → void*   (construct on heap)
//   - zs_reflect_<Type>_destroy(void*)       → void    (delete)
//   - zs_reflect_<Type>_get_<field>(void*)   → <ctype> (getter)
//   - zs_reflect_<Type>_set_<field>(void*, <ctype>)    (setter)
//   - zs_reflect_<Type>_invoke_<method>(void*, ...)    (invoker)
//   - zs_reflect_<Type>_type_name()          → const char*
//   - zs_reflect_<Type>_field_names()        → const char* (comma-separated)
//   - zs_reflect_<Type>_method_names()       → const char* (comma-separated)
//
// This header is std-free.  Only ZpcMeta.hpp and <cstring> are needed.

#pragma once

#include "zensim/ZpcMeta.hpp"

#include <cstring>  // ::strlen, ::memcpy

// ---- ZPC_EXPORT (should already be defined by Platform.hpp, provide fallback) ----
#ifndef ZPC_EXPORT
#  if defined(_MSC_VER)
#    define ZPC_EXPORT __declspec(dllexport)
#  elif defined(__GNUC__) || defined(__clang__)
#    define ZPC_EXPORT __attribute__((visibility("default")))
#  else
#    define ZPC_EXPORT
#  endif
#endif

// ---- ZPC_REFLECT_PY_API: wraps extern "C" ZPC_EXPORT for Python-facing symbols ----
#define ZPC_REFLECT_PY_API extern "C" ZPC_EXPORT

namespace zs {
namespace reflect {
namespace py {

  // ------------------------------------------------------------------
  // Supported C types for Python ctypes transport
  // ------------------------------------------------------------------

  /// Enumeration mirrored in the generated Python wrapper to select
  /// the correct ctypes type for function parameters / return values.
  enum class CType : int {
    Void   = 0,
    Bool   = 1,
    Int8   = 2,
    Int16  = 3,
    Int32  = 4,
    Int64  = 5,
    UInt8  = 6,
    UInt16 = 7,
    UInt32 = 8,
    UInt64 = 9,
    Float  = 10,
    Double = 11,
    CStr   = 12,  // const char*
    Ptr    = 13,  // void*
  };

  // ------------------------------------------------------------------
  // Helper: field-descriptor for introspection from Python side
  // ------------------------------------------------------------------

  struct FieldDescriptor {
    const char* name;
    CType       ctype;
    zs::size_t  offset;
    zs::size_t  size;
    bool        readonly;
  };

  struct MethodDescriptor {
    const char*  name;
    CType        returnCType;
    int          paramCount;
    const CType* paramCTypes;  // array of paramCount elements
  };

  // ------------------------------------------------------------------
  // Helper: map C++ types to CType (used by codegen)
  // ------------------------------------------------------------------

  /// Primary template — fallback to Ptr for unknown types.
  template <typename T> struct CTypeMap {
    static constexpr CType value = CType::Ptr;
  };

  // Standard C/C++ types
  template <> struct CTypeMap<bool>         { static constexpr CType value = CType::Bool;   };
  template <> struct CTypeMap<float>        { static constexpr CType value = CType::Float;  };
  template <> struct CTypeMap<double>       { static constexpr CType value = CType::Double; };
  template <> struct CTypeMap<const char*>  { static constexpr CType value = CType::CStr;   };
  template <> struct CTypeMap<char*>        { static constexpr CType value = CType::CStr;   };
  template <> struct CTypeMap<void*>        { static constexpr CType value = CType::Ptr;    };

  // Signed integer types (zs aliases + fallback standard types)
  template <> struct CTypeMap<signed char>       { static constexpr CType value = CType::Int8;  };
  template <> struct CTypeMap<signed short>      { static constexpr CType value = CType::Int16; };
  template <> struct CTypeMap<signed int>        { static constexpr CType value = CType::Int32; };
  template <> struct CTypeMap<signed long long>  { static constexpr CType value = CType::Int64; };

  // Unsigned integer types
  template <> struct CTypeMap<unsigned char>      { static constexpr CType value = CType::UInt8;  };
  template <> struct CTypeMap<unsigned short>     { static constexpr CType value = CType::UInt16; };
  template <> struct CTypeMap<unsigned int>       { static constexpr CType value = CType::UInt32; };
  template <> struct CTypeMap<unsigned long long> { static constexpr CType value = CType::UInt64; };

  // Handle `long` / `unsigned long` portably (LP64 vs LLP64)
  template <> struct CTypeMap<long> {
    static constexpr CType value = sizeof(long) == 8 ? CType::Int64 : CType::Int32;
  };
  template <> struct CTypeMap<unsigned long> {
    static constexpr CType value = sizeof(unsigned long) == 8 ? CType::UInt64 : CType::UInt32;
  };

  // char (signedness is implementation-defined)
  template <> struct CTypeMap<char> {
    static constexpr CType value = CType::Int8;
  };

  // ------------------------------------------------------------------
  // String buffer helper (for returning strings to Python safely)
  // ------------------------------------------------------------------

  /// Copy `s` into a thread-local buffer and return a stable pointer.
  /// The pointer is valid until the next call from the same thread.
  inline const char* returnString(const char* s) {
    static constexpr zs::size_t kBufSize = 4096;
    thread_local char buf[kBufSize];
    if (!s) { buf[0] = '\0'; return buf; }
    zs::size_t len = ::strlen(s);
    if (len >= kBufSize) len = kBufSize - 1;
    ::memcpy(buf, s, len);
    buf[len] = '\0';
    return buf;
  }

  /// Join `count` C-strings with a separator into a thread-local buffer.
  inline const char* returnJoined(const char* const* parts, int count,
                                  const char* sep = ",") {
    static constexpr zs::size_t kBufSize = 4096;
    thread_local char buf[kBufSize];
    buf[0] = '\0';
    zs::size_t pos = 0;
    zs::size_t sepLen = sep ? ::strlen(sep) : 0;
    for (int i = 0; i < count && pos < kBufSize - 1; ++i) {
      if (i > 0 && sepLen > 0 && pos + sepLen < kBufSize - 1) {
        ::memcpy(buf + pos, sep, sepLen);
        pos += sepLen;
      }
      if (!parts[i]) continue;
      zs::size_t pLen = ::strlen(parts[i]);
      if (pos + pLen >= kBufSize) pLen = kBufSize - 1 - pos;
      ::memcpy(buf + pos, parts[i], pLen);
      pos += pLen;
    }
    buf[pos] = '\0';
    return buf;
  }

}  // namespace py
}  // namespace reflect
}  // namespace zs
