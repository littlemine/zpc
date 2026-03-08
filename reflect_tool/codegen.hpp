// Copyright (c) zpc contributors. Licensed under the MIT License.
#pragma once
#include "utils.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace zs::reflect_tool {

  struct CollectedType; // forward decl (defined in parser.hpp)
  struct ReflectionModel;

  // -----------------------------------------------------------------------
  // Compiler state — accumulated across all processed TUs
  // -----------------------------------------------------------------------

  struct CodeCompilerState {
    /// All reflected types collected from every source file.
    std::vector<CollectedType> reflectedTypes;

    /// Set of type hashes already emitted (dedup).
    std::unordered_map<uint64_t, bool> emittedHashes;

    void init(const std::vector<std::string>& /*inputSources*/,
              const std::string& /*templateInclude*/) {
      reflectedTypes.clear();
      emittedHashes.clear();
    }
  };

  // -----------------------------------------------------------------------
  // Code generation entry points
  // -----------------------------------------------------------------------

  /// Emit a per-source `.reflected.hpp` header with TypeInfoOf specializations
  /// and AccessorOf specializations (field getters/setters + method invokers).
  void emit_reflected_header(const std::string& outputPath,
                             const std::vector<CollectedType>& types,
                             bool verbose);

  /// Emit the aggregated static-registration `.cpp` file.
  void emit_register_source(const std::string& outputPath,
                            const ReflectionModel& model,
                            const CodeCompilerState& state,
                            const std::string& targetName,
                            bool verbose);

  /// Emit a `.reflected_py.cpp` file with extern "C" ZPC_EXPORT functions
  /// for every reflected type — create/destroy, field get/set, method invoke —
  /// suitable for consumption by Python ctypes.
  void emit_python_bindings(const std::string& outputPath,
                            const std::vector<CollectedType>& allTypes,
                            const std::string& targetName,
                            bool verbose);

  /// Emit a Python wrapper module `<target>_reflect.py` that loads the shared
  /// library and wraps each reflected type into a Python class with properties
  /// and methods.
  void emit_python_wrapper(const std::string& outputPath,
                           const std::vector<CollectedType>& allTypes,
                           const std::string& libName,
                           bool verbose);

} // namespace zs::reflect_tool
