// Copyright (c) zpc contributors. Licensed under the MIT License.
#pragma once
#include "meta_data.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <cassert>
#include <clang/AST/Expr.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Type.h>

namespace zs::reflect_tool {

  // -----------------------------------------------------------------------
  // String / path utilities
  // -----------------------------------------------------------------------

  std::vector<std::string> get_parser_command_args(
      const std::string& cppVersion,
      std::vector<std::string>& includeDirs,
      std::vector<std::string>& preIncludeHeaders,
      bool verbose);

  std::string get_file_path_in_header_output(std::string_view filename,
                                              std::string_view outputDir);

  std::string relative_path_to_header_output(std::string_view absPath,
                                              std::string_view outputDir);

  std::string trim(const std::string& str);
  void replace_all(std::string& outStr, const std::string& from, const std::string& to);
  void truncate_file(const std::string& path);
  bool mkdirs(std::string_view path);

  std::string normalize_filename(std::string_view input);
  std::string convert_to_valid_cpp_var_name(std::string_view typeName);
  std::string clang_expr_to_string(const clang::Expr* expr);
  std::string clang_type_name_no_tag(const clang::QualType& type);

  // -----------------------------------------------------------------------
  // Metadata extraction from clang AST
  // -----------------------------------------------------------------------

  /// Check if a Decl has an annotate attribute whose string starts with a
  /// known reflection prefix ("zs_reflect", "#field", "#property", …).
  bool has_reflect_annotation(const clang::Decl* decl);

  /// Check if a Decl is explicitly excluded via ZS_NO_REFLECT.
  bool has_no_reflect(const clang::Decl* decl);

  /// Extract the raw annotation string from a Decl (first AnnotateAttr).
  std::string get_annotation_string(const clang::Decl* decl);

  /// Parse all AnnotateAttr on a Decl into a MetadataContainer.
  MetadataContainer parse_decl_metadata(const clang::Decl* decl);

  // -----------------------------------------------------------------------
  // FNV-1a Hash
  // -----------------------------------------------------------------------

  namespace detail {
    template <typename T> struct FNV1aConstants;
    template <> struct FNV1aConstants<uint32_t> {
      static constexpr uint32_t offset = 0x811c9dc5U;
      static constexpr uint32_t prime  = 0x1000193U;
    };
    template <> struct FNV1aConstants<uint64_t> {
      static constexpr uint64_t offset = 0xcbf29ce484222325ULL;
      static constexpr uint64_t prime  = 0x100000001b3ULL;
    };
  } // namespace detail

  struct FNV1aHash {
    constexpr uint64_t operator()(std::string_view str) const noexcept {
      uint64_t h = detail::FNV1aConstants<uint64_t>::offset;
      for (unsigned char c : str) {
        h ^= c;
        h *= detail::FNV1aConstants<uint64_t>::prime;
      }
      return h;
    }
  };

} // namespace zs::reflect_tool
