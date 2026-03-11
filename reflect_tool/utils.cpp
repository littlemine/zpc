// Copyright (c) zpc contributors. Licensed under the MIT License.
#include "utils.hpp"
#include <clang/AST/PrettyPrinter.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include "zensim/zpc_tpls/fmt/format.h"

namespace zs::reflect_tool {

  // -----------------------------------------------------------------------
  // Compiler argument construction
  // -----------------------------------------------------------------------

  std::vector<std::string> get_parser_command_args(
      const std::string& cppVersion,
      std::vector<std::string>& includeDirs,
      std::vector<std::string>& preIncludeHeaders,
      bool verbose) {
    std::vector<std::string> result;

    // Force C++ mode (clang defaults .h to C).
    result.push_back("-x");
    result.push_back("c++");

    result.push_back("-Wno-pragma-once-outside-header");
    result.push_back(fmt::format("-std=c++{}", cppVersion));

    // These defines are only set when parsing via the reflect tool.
    result.push_back("-DZS_REFLECT_PROCESSING=1");
    result.push_back("-DWITH_REFLECT=1");

    for (auto& dir : includeDirs)
      result.push_back(fmt::format("-I{}", dir));

    for (auto& header : preIncludeHeaders) {
      result.push_back("-include");
      result.push_back(header);
    }

    return result;
  }

  // -----------------------------------------------------------------------
  // Path helpers
  // -----------------------------------------------------------------------

  std::string get_file_path_in_header_output(std::string_view filename,
                                              std::string_view outputDir) {
    return (std::filesystem::path(outputDir) / std::filesystem::path(filename)).string();
  }

  std::string relative_path_to_header_output(std::string_view absPath,
                                              std::string_view outputDir) {
    return std::filesystem::relative(
        std::filesystem::path(absPath),
        std::filesystem::path(outputDir)).string();
  }

  std::string trim(const std::string& str) {
    size_t s = 0, e = str.size();
    while (s < e && std::isspace(static_cast<unsigned char>(str[s]))) ++s;
    while (e > s && std::isspace(static_cast<unsigned char>(str[e - 1]))) --e;
    return str.substr(s, e - s);
  }

  void replace_all(std::string& outStr, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = outStr.find(from, pos)) != std::string::npos) {
      outStr.replace(pos, from.length(), to);
      pos += to.length();
    }
  }

  void truncate_file(const std::string& path) {
    std::ofstream s(path, std::ios::out | std::ios::trunc);
    s.close();
  }

  bool mkdirs(std::string_view path) {
    try {
      return std::filesystem::create_directories(std::filesystem::path(path));
    } catch (const std::filesystem::filesystem_error& err) {
      std::cerr << "mkdirs error: " << err.what() << "\n";
      return false;
    }
  }

  std::string normalize_filename(std::string_view input) {
    auto p = std::filesystem::path(input).lexically_normal().filename().string();
    // Remove extension for a clean base name
    auto dot = p.rfind('.');
    if (dot != std::string::npos) p = p.substr(0, dot);
    return p;
  }

  std::string convert_to_valid_cpp_var_name(std::string_view typeName) {
    std::string var;
    bool lastWasSep = false;
    for (size_t i = 0; i < typeName.size(); ++i) {
      char ch = typeName[i];
      if (typeName.substr(i, 6) == "struct"
          && (i + 6 == typeName.size() || typeName[i + 6] == ' ')) {
        var += "struct_"; i += 6;
      } else if (typeName.substr(i, 5) == "class"
                 && (i + 5 == typeName.size() || typeName[i + 5] == ' ')) {
        var += "class_"; i += 5;
      } else if (typeName.substr(i, 5) == "union"
                 && (i + 5 == typeName.size() || typeName[i + 5] == ' ')) {
        var += "union_"; i += 5;
      } else if (std::isalnum(static_cast<unsigned char>(ch))) {
        var += ch; lastWasSep = false;
      } else if (ch == ':' || ch == ' ') {
        if (!lastWasSep && !var.empty() && var.back() != '_') var += '_';
        lastWasSep = (ch == ':');
      } else if (ch == '<') { var += "LAB"; }
      else if (ch == '>') { var += "RAB"; }
      else if (ch == '*') { var += "Ptr"; }
      else if (ch == '&') { var += "Ref"; }
    }
    if (!var.empty() && var.back() == '_') var.pop_back();
    if (!var.empty() && std::isdigit(static_cast<unsigned char>(var[0]))) var = "_" + var;
    return var;
  }

  std::string clang_expr_to_string(const clang::Expr* expr) {
    if (!expr) return "nullptr";
    clang::LangOptions lo;
    lo.CPlusPlus = true;
    clang::PrintingPolicy pp(lo);
    std::string s;
    llvm::raw_string_ostream stream(s);
    expr->printPretty(stream, nullptr, pp);
    return s;
  }

  std::string clang_type_name_no_tag(const clang::QualType& type) {
    clang::LangOptions lo;
    lo.CPlusPlus = true;
    clang::PrintingPolicy pp(lo);
    pp.SuppressTagKeyword = true;
    pp.Bool = true;  // print 'bool' instead of '_Bool'
    return type.getAsString(pp);
  }

  // -----------------------------------------------------------------------
  // Annotation helpers
  // -----------------------------------------------------------------------

  bool has_reflect_annotation(const clang::Decl* decl) {
    for (auto* attr : decl->attrs()) {
      if (auto* ann = llvm::dyn_cast<clang::AnnotateAttr>(attr)) {
        auto s = ann->getAnnotation();
        if (s.starts_with("zs_reflect") || s.starts_with("#struct")
            || s.starts_with("#field") || s.starts_with("#property")
            || s.starts_with("#method") || s.starts_with("#function")
            || s.starts_with("#trait") || s.starts_with("#enum")
            // Backward-compat with prototype macros
            || s == "reflect" || s.starts_with("property:")
            || s == "serialize")
          return true;
      }
    }
    return false;
  }

  bool has_no_reflect(const clang::Decl* decl) {
    for (auto* attr : decl->attrs()) {
      if (auto* ann = llvm::dyn_cast<clang::AnnotateAttr>(attr)) {
        auto s = ann->getAnnotation();
        if (s == "zs_no_reflect" || s == "no_reflect") return true;
      }
    }
    return false;
  }

  std::string get_annotation_string(const clang::Decl* decl) {
    for (auto* attr : decl->attrs()) {
      if (auto* ann = llvm::dyn_cast<clang::AnnotateAttr>(attr))
        return ann->getAnnotation().str();
    }
    return {};
  }

  MetadataContainer parse_decl_metadata(const clang::Decl* decl) {
    MetadataContainer merged;
    for (auto* attr : decl->attrs()) {
      if (auto* ann = llvm::dyn_cast<clang::AnnotateAttr>(attr)) {
        std::string s = ann->getAnnotation().str();
        if (s.empty() || s == "zs_reflect" || s == "reflect"
            || s == "zs_no_reflect" || s == "no_reflect"
            || s == "zs_reflect_enum")
          continue;
        auto c = parse_metadata_dsl(s);
        if (c.type != MetadataType::None && merged.type == MetadataType::None)
          merged.type = c.type;
        for (auto& [k, v] : c.properties) merged.properties.insert_or_assign(k, v);
      }
    }
    return merged;
  }

} // namespace zs::reflect_tool
