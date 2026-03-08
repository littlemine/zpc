// Copyright (c) zpc contributors. Licensed under the MIT License.
// ZpcReflectAnnotations.hpp — User-facing annotation macros for zpc reflection.
//
// Usage:
//   #include <zensim/reflect/ZpcReflectAnnotations.hpp>
//
//   struct ZS_REFLECT MyStruct {
//       ZS_PROPERTY(name = "pos", serializable = true)
//       float x;
//
//       ZS_FIELD(transient = true)
//       int cache_;
//
//       ZS_METHOD(category = "transform")
//       void translate(float dx, float dy, float dz);
//   };
//
// The annotation DSL uses `key = value` pairs inside the macro parentheses.
// Supported value types: strings ("..."), integers, floats, enums, lists ((...)).
// Example: ZS_PROPERTY(name = "id", priority = 10, tags = ("editor", "save"))
//
// During normal compilation these macros expand to nothing (zero overhead).
// When processed by zpc_reflect_tool, they are parsed through
// __attribute__((annotate(...))) to extract metadata.

#pragma once

// ---------------------------------------------------------------------------
// Guard: ZS_REFLECT_PROCESSING is defined only when the reflection tool parses
// the source.  During regular compilation the macros are harmless no-ops.
// ---------------------------------------------------------------------------

#if defined(ZS_REFLECT_PROCESSING) || defined(__clang__)
// Clang supports __attribute__((annotate(...))).
// MSVC does not, but when building normally on MSVC we use the #else branch.
// The reflect tool always uses clang, so these are always available there.

/// Mark a struct / class / union for reflection.
#define ZS_REFLECT __attribute__((annotate("zs_reflect")))

/// Mark a struct / class / enum with a custom metadata DSL string.
/// Example: ZS_REFLECT_BODY(#struct name = "MyType", serializable = true)
#define ZS_REFLECT_BODY(...) __attribute__((annotate("zs_reflect:" #__VA_ARGS__)))

/// Field-level annotation with metadata DSL.
/// Example: ZS_PROPERTY(name = "position", tooltip = "World position")
#define ZS_PROPERTY(...) __attribute__((annotate("#property " #__VA_ARGS__)))

/// Field-level annotation (alias for ZS_PROPERTY, no extra metadata).
/// Example: ZS_FIELD(transient = true)
#define ZS_FIELD(...) __attribute__((annotate("#field " #__VA_ARGS__)))

/// Method-level annotation with metadata DSL.
/// Example: ZS_METHOD(category = "math")
#define ZS_METHOD(...) __attribute__((annotate("#method " #__VA_ARGS__)))

/// Mark a field / method as serializable.
#define ZS_SERIALIZE __attribute__((annotate("#field serializable = true")))

/// Exclude a field / method from reflection even if the enclosing type is reflected.
#define ZS_NO_REFLECT __attribute__((annotate("zs_no_reflect")))

/// Mark an enum for reflection.
#define ZS_REFLECT_ENUM __attribute__((annotate("zs_reflect_enum")))

/// Trait annotation — attach trait metadata to a declaration.
/// Example: ZS_TRAIT(kind = "Renderable")
#define ZS_TRAIT(...) __attribute__((annotate("#trait " #__VA_ARGS__)))

#else
// Fallback for compilers that do not support __attribute__((annotate(...))).
// These expand to nothing — zero runtime cost.

#define ZS_REFLECT
#define ZS_REFLECT_BODY(...)
#define ZS_PROPERTY(...)
#define ZS_FIELD(...)
#define ZS_METHOD(...)
#define ZS_SERIALIZE
#define ZS_NO_REFLECT
#define ZS_REFLECT_ENUM
#define ZS_TRAIT(...)

#endif

// ---------------------------------------------------------------------------
// Convenience: backward-compatible short aliases.
// Define ZS_NO_SHORT_MACROS before including this header to suppress these.
// ---------------------------------------------------------------------------
#ifndef ZS_NO_SHORT_MACROS
#  ifndef REFLECT
#    define REFLECT ZS_REFLECT
#  endif
#  ifndef PROPERTY
#    define PROPERTY(...) ZS_PROPERTY(__VA_ARGS__)
#  endif
#  ifndef SERIALIZE
#    define SERIALIZE ZS_SERIALIZE
#  endif
#  ifndef NO_REFLECT
#    define NO_REFLECT ZS_NO_REFLECT
#  endif
#endif  // ZS_NO_SHORT_MACROS
