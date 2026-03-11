// Copyright (c) zpc contributors. Licensed under the MIT License.
// zpc_reflect_tool — Clang-based reflection code generator.
//
// This tool parses C++ source/header files that use the ZS_REFLECT /
// ZS_PROPERTY / … annotation macros, collects struct/class/enum metadata via
// clang's libTooling, and emits `.reflected.hpp` files with compile-time and
// runtime reflection data.
//
// Usage:
//   zpc_reflect_tool -S <file1.hpp> -S <file2.hpp> \
//     -o <output_dir> \
//     --generated-source-path <register.cpp> \
//     -T <TargetName> \
//     [-I <include_dir>]... \
//     [-H <pre-include-header>]... \
//     [--stdc++ 17] [-v]

#ifdef __clang__
#define _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
#define _HAS_EXCEPTIONS 1
#endif

#include "zensim/zpc_tpls/argparse/argparse.hpp"
#include "zensim/zpc_tpls/fmt/format.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <set>
#include <algorithm>
#include "parser.hpp"
#include "codegen.hpp"
#include "utils.hpp"

static std::optional<std::string> read_source_file(const std::string& filepath) {
  std::ifstream file(filepath);
  if (!file.is_open()) return std::nullopt;
  std::stringstream buf;
  buf << file.rdbuf();
  return buf.str();
}

int main(int argc, char* argv[]) {
  // ---- argument parsing ---------------------------------------------------
  std::vector<std::string> inputSources;
  std::string outputDir;
  std::string registerSourcePath;
  std::string cppVersion = "17";
  bool verbose = false;
  std::vector<std::string> includeDirs;
  std::vector<std::string> preIncludeHeaders;
  std::string targetName;
  std::string templateInclude;
  std::string depfile;  // for CMake / Ninja depfile support
  bool pythonBindings = false;
  std::string pyBindingsOutputDir;
  std::string pyLibName;
  std::vector<std::string> scanDirs;  // directories to scan for annotated files

  argparse::ArgumentParser parser("zpc_reflect_tool", "1.0");

  parser.add_argument("-S", "--input-source")
      .help("Source file to process for reflection")
      .append()
      .required()
      .store_into(inputSources);

  parser.add_argument("-o", "--header-output")
      .help("Output directory for generated headers")
      .required()
      .store_into(outputDir);

  parser.add_argument("--generated-source-path")
      .help("Path for the generated static-registration .cpp file")
      .required()
      .store_into(registerSourcePath);

  parser.add_argument("--stdc++")
      .help("C++ standard version (default: 17)")
      .default_value(std::string("17"))
      .store_into(cppVersion);

  parser.add_argument("-v", "--verbose")
      .help("Verbose output")
      .flag()
      .store_into(verbose);

  parser.add_argument("-I", "--include-dirs")
      .help("Additional include directories")
      .append()
      .store_into(includeDirs);

  parser.add_argument("-H", "--pre-include-header")
      .help("Automatically include these headers in every TU")
      .append()
      .store_into(preIncludeHeaders);

  parser.add_argument("-T", "--target-name")
      .help("CMake target name (used for subdirectory naming)")
      .required()
      .store_into(targetName);

  parser.add_argument("--template-include")
      .help("Extra include in generated templates")
      .default_value(std::string(""))
      .store_into(templateInclude);

  parser.add_argument("--depfile")
      .help("Write a Ninja/Make-style depfile for incremental builds")
      .default_value(std::string(""))
      .store_into(depfile);

  parser.add_argument("--python-bindings")
      .help("Generate Python ctypes C-ABI bindings")
      .flag()
      .store_into(pythonBindings);

  parser.add_argument("--py-output-dir")
      .help("Output directory for Python binding files (.cpp + .py)")
      .default_value(std::string(""))
      .store_into(pyBindingsOutputDir);

  parser.add_argument("--py-lib-name")
      .help("Shared library name for Python ctypes loading (without extension)")
      .default_value(std::string(""))
      .store_into(pyLibName);

  parser.add_argument("--scan-dir")
      .help("Recursively scan directory for files containing ZS_REFLECT annotations; "
            "discovered files are added to the source list automatically")
      .append()
      .store_into(scanDirs);

  try {
    parser.parse_args(argc, argv);
  } catch (const std::exception& err) {
    std::cerr << "Error: " << err.what() << "\n" << parser;
    return 1;
  }

  if (verbose) {
    fmt::print("zpc_reflect_tool v1.0\n");
    fmt::print("  target    : {}\n", targetName);
    fmt::print("  output    : {}\n", outputDir);
    fmt::print("  register  : {}\n", registerSourcePath);
    fmt::print("  stdc++    : {}\n", cppVersion);
    for (auto& s : inputSources) fmt::print("  source    : {}\n", s);
    for (auto& d : includeDirs) fmt::print("  -I        : {}\n", d);
    for (auto& h : preIncludeHeaders) fmt::print("  -H        : {}\n", h);
  }

  // ---- scan directories for annotated files ------------------------------
  if (!scanDirs.empty()) {
    for (auto& dir : scanDirs) {
      if (!std::filesystem::exists(dir)) {
        std::cerr << fmt::format("Scan directory does not exist: {}\n", dir);
        continue;
      }
      for (auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        // Only scan C++ header/source files
        if (ext != ".hpp" && ext != ".h" && ext != ".hxx" && ext != ".hh"
            && ext != ".cpp" && ext != ".cxx" && ext != ".cc")
          continue;

        // Quick text check: does the file contain a reflection annotation?
        std::ifstream probe(entry.path());
        if (!probe.is_open()) continue;
        std::string line;
        bool hasAnnotation = false;
        while (std::getline(probe, line)) {
          if (line.find("ZS_REFLECT") != std::string::npos
              || line.find("ZS_REFLECT_ENUM") != std::string::npos) {
            hasAnnotation = true;
            break;
          }
        }
        if (hasAnnotation) {
          std::string absPath = std::filesystem::canonical(entry.path()).string();
          // Avoid duplicates with already-listed sources
          bool dup = false;
          for (auto& existing : inputSources) {
            if (std::filesystem::equivalent(existing, absPath)) { dup = true; break; }
          }
          if (!dup) {
            inputSources.push_back(absPath);
            if (verbose)
              fmt::print("  [scan] Discovered annotated file: {}\n", absPath);
          }
        }
      }
    }
  }

  // ---- per-file processing ------------------------------------------------
  zs::reflect_tool::ReflectionModel model{};
  zs::reflect_tool::CodeCompilerState compilerState;
  compilerState.init(inputSources, templateInclude);
  auto compilerArgs = zs::reflect_tool::get_parser_command_args(
      cppVersion, includeDirs, preIncludeHeaders, verbose);

  bool anyError = false;
  std::set<std::string> allFileDeps;  // all files discovered by clang SourceManager

  for (auto& inputSource : inputSources) {
    auto sourceOpt = read_source_file(inputSource);
    if (!sourceOpt) {
      std::cerr << fmt::format("Cannot read source file: {}\n", inputSource);
      anyError = true;
      continue;
    }
    if (verbose) fmt::print("Processing: {}\n", inputSource);

    // Compute output path
    const std::string subDir = fmt::format("reflect/{}", targetName);
    const std::string templateHeaderDir
        = zs::reflect_tool::get_file_path_in_header_output(subDir, outputDir);
    const std::string genHeaderPath = fmt::format(
        "{}/{}.reflected.hpp", templateHeaderDir,
        zs::reflect_tool::normalize_filename(inputSource));
    zs::reflect_tool::mkdirs(templateHeaderDir);
    zs::reflect_tool::truncate_file(genHeaderPath);

    model.generatedHeaders.insert(genHeaderPath);
    model.inputSourcePaths.push_back(inputSource);
    model.debugName = inputSource;

    if (!clang::tooling::runToolOnCodeWithArgs(
            std::make_unique<zs::reflect_tool::ReflectionGeneratorAction>(
                compilerState, genHeaderPath, verbose, &allFileDeps),
            sourceOpt->c_str(), compilerArgs, inputSource.c_str())) {
      std::cerr << fmt::format("Clang tooling failed on: {}\n", inputSource);
      anyError = true;
    }
  }

  // ---- emit registration source file --------------------------------------
  zs::reflect_tool::emit_register_source(
      registerSourcePath, model, compilerState, targetName, verbose);


  // ---- emit Python bindings (C-ABI + Python wrapper) ----------------------
  if (pythonBindings && !compilerState.reflectedTypes.empty()) {
    std::string pyOutDir = pyBindingsOutputDir.empty() ? outputDir : pyBindingsOutputDir;
    std::string libNameFinal = pyLibName.empty()
        ? fmt::format("zpc_reflect_py_{}", targetName)
        : pyLibName;

    zs::reflect_tool::mkdirs(pyOutDir);

    // C-ABI .cpp
    std::string pyBindingsCpp = fmt::format("{}/{}_py_bindings.cpp",
                                            pyOutDir, targetName);
    zs::reflect_tool::emit_python_bindings(
        pyBindingsCpp, compilerState.reflectedTypes, targetName, verbose);

    // Python wrapper .py
    std::string pyWrapperPath = fmt::format("{}/{}_reflect.py",
                                            pyOutDir, targetName);
    zs::reflect_tool::emit_python_wrapper(
        pyWrapperPath, compilerState.reflectedTypes, libNameFinal, verbose);
  }
  // ---- emit depfile (for CMake / Ninja incremental builds) ----------------
  // The depfile tells Ninja/Make the full set of files that, if changed,
  // should trigger a re-run of zpc_reflect_tool.  This includes:
  //   - The explicitly-listed input source files
  //   - ALL files #include'd by those sources (discovered via clang SourceManager)
  // This ensures that modifying any header used by an annotated file will
  // automatically cause reflection re-generation.
  if (!depfile.empty()) {
    // Also add all explicitly-listed input sources (in case clang didn't
    // open some of them due to errors).
    for (auto& s : inputSources) {
      std::string path = s;
      std::replace(path.begin(), path.end(), '\\', '/');
      allFileDeps.insert(path);
    }

    // Escape spaces in paths for Make/Ninja depfile format
    auto escape_dep_path = [](const std::string& p) -> std::string {
      std::string out;
      for (char c : p) {
        if (c == ' ' || c == '#' || c == '\\') out += '\\';
        out += c;
      }
      return out;
    };

    std::ofstream df(depfile, std::ios::out | std::ios::trunc);
    if (df.is_open()) {
      // Primary output depends on all discovered files
      df << escape_dep_path(registerSourcePath) << ":";
      for (auto& dep : allFileDeps)
        df << " \\\n  " << escape_dep_path(dep);
      df << "\n\n";

      // Each generated header also depends on all discovered files
      for (auto& h : model.generatedHeaders) {
        df << escape_dep_path(h) << ":";
        for (auto& dep : allFileDeps)
          df << " \\\n  " << escape_dep_path(dep);
        df << "\n\n";
      }

      if (verbose)
        fmt::print("  Wrote depfile with {} dependencies → {}\n",
                   allFileDeps.size(), depfile);
    }
  }

  if (verbose) fmt::print("zpc_reflect_tool done.  {} types reflected.\n",
                           compilerState.reflectedTypes.size());
  return anyError ? 1 : 0;
}
