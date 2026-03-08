cmake_minimum_required(VERSION 3.20)

# ============================================================
# zpc_reflect_tool — Clang-based reflection code generator
# ============================================================
#
# This builds the `zpc_reflect_tool` executable that parses C++
# source files for ZS_REFLECT annotations and generates
# `.reflected.hpp` headers + a registration `.cpp` file.
#
# It requires LLVM/Clang libraries via the `zpc_jit_deps` target.

if(NOT TARGET zpc_jit_deps)
  message(STATUS "[zpc_reflect_tool] zpc_jit_deps not available — skipping reflect_tool build")
  return()
endif()

add_executable(zpc_reflect_tool
  main.cpp
  parser.cpp
  codegen.cpp
  meta_data.cpp
  utils.cpp
)

target_link_libraries(zpc_reflect_tool PRIVATE zpc_jit_deps)

set_target_properties(zpc_reflect_tool PROPERTIES
  LINKER_LANGUAGE CXX
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Include zpc headers (for fmt, rapidjson, argparse, etc.)
target_include_directories(zpc_reflect_tool PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)

# Fix for MSVC STL + Clang combination
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_definitions(zpc_reflect_tool PRIVATE
    _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
    _HAS_EXCEPTIONS=1
  )
endif()

# On MSVC, silence some warnings in LLVM/Clang headers
if(MSVC)
  target_compile_options(zpc_reflect_tool PRIVATE /W3 /wd4624 /wd4291 /wd4141)
endif()

message(STATUS "[zpc] Built zpc_reflect_tool executable")
