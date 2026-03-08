# ============================================================
# ZpcReflection.cmake — CMake utilities for zpc reflection
# ============================================================
#
# This module provides the function `zpc_target_reflect_sources()` that
# integrates the clang-based reflection code generator into a CMake build.
#
# Usage:
#   include(ZpcReflection)
#   add_library(my_lib src/Foo.cpp src/Bar.cpp)
#   zpc_target_reflect_sources(my_lib
#     SOURCES src/Foo.hpp src/Bar.hpp
#     [INCLUDE_DIRS dir1 dir2 ...]
#     [PRE_INCLUDE_HEADERS hdr1.h hdr2.h ...]
#     [CXX_STANDARD 17]
#     [VERBOSE]
#     [PYTHON_BINDINGS]
#     [PYTHON_LIB_NAME <name>]
#   )
#
# What it does:
#   1. For each listed source file, adds a custom command that runs
#      `zpc_reflect_tool` to produce a `.reflected.hpp` in the build tree.
#   2. Adds a custom command to produce a registration `.cpp` file.
#   3. The custom commands DEPEND on the input source files, so they are
#      automatically re-run whenever the source changes (Ninja / Make).
#   4. Adds the generated directory to the target's include path so that
#      generated headers can be `#include`-d from user code.
#   5. Optionally generates a depfile for Ninja for header-level deps.
#
# Requirements:
#   - The `zpc_reflect_tool` target must exist in the build.
#   - LLVM/Clang must be available (the tool links against it).
#

# Guard against multiple inclusion
if(_ZPC_REFLECTION_CMAKE_INCLUDED)
  return()
endif()
set(_ZPC_REFLECTION_CMAKE_INCLUDED TRUE)

# ----------------------------------------------------------------
# zpc_target_reflect_sources(<target>
#   SOURCES <file1> [<file2> ...]
#   [INCLUDE_DIRS <dir1> ...]
#   [PRE_INCLUDE_HEADERS <header1> ...]
#   [CXX_STANDARD <17|20|23>]
#   [VERBOSE]
# )
# ----------------------------------------------------------------
function(zpc_target_reflect_sources target)
  if(NOT TARGET zpc_reflect_tool)
    message(WARNING "[ZpcReflection] zpc_reflect_tool target not found — skipping reflection for ${target}")
    return()
  endif()

  cmake_parse_arguments(PARSE_ARGV 1 ARG
    "VERBOSE;PYTHON_BINDINGS"               # options (flags)
    "CXX_STANDARD;PYTHON_LIB_NAME"          # one-value keywords
    "SOURCES;SCAN_DIRS;INCLUDE_DIRS;PRE_INCLUDE_HEADERS" # multi-value keywords
  )

  # ---- Auto-discover sources from SCAN_DIRS ------------------------------
  if(ARG_SCAN_DIRS)
    foreach(_scan_dir IN LISTS ARG_SCAN_DIRS)
      if(NOT IS_ABSOLUTE "${_scan_dir}")
        set(_scan_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_scan_dir}")
      endif()
      file(GLOB_RECURSE _found_files
        "${_scan_dir}/*.hpp"
        "${_scan_dir}/*.h"
        "${_scan_dir}/*.hxx"
      )
      foreach(_f IN LISTS _found_files)
        # Quick grep: only add files that contain ZS_REFLECT
        file(STRINGS "${_f}" _lines REGEX "ZS_REFLECT" LIMIT_COUNT 1)
        if(_lines)
          list(APPEND ARG_SOURCES "${_f}")
        endif()
      endforeach()
    endforeach()
    if(ARG_SOURCES)
      list(REMOVE_DUPLICATES ARG_SOURCES)
    endif()
  endif()

  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "[ZpcReflection] zpc_target_reflect_sources: SOURCES or SCAN_DIRS is required")
  endif()

  if(NOT ARG_CXX_STANDARD)
    set(ARG_CXX_STANDARD "17")
  endif()

  # ---- Directories -------------------------------------------------------
  set(_reflect_output_dir "${CMAKE_CURRENT_BINARY_DIR}/zpc_reflected/${target}")
  set(_reflect_gen_src    "${_reflect_output_dir}/zpc_reflect_register_${target}.cpp")
  set(_reflect_stamp      "${_reflect_output_dir}/.zpc_reflect_stamp")

  file(MAKE_DIRECTORY "${_reflect_output_dir}")

  # ---- Build zpc_reflect_tool command line --------------------------------
  set(_tool_cmd "$<TARGET_FILE:zpc_reflect_tool>")

  set(_src_args "")
  set(_all_source_deps "")
  set(_all_generated_headers "")

  foreach(_src IN LISTS ARG_SOURCES)
    # Make path absolute
    if(NOT IS_ABSOLUTE "${_src}")
      set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
    endif()
    list(APPEND _src_args "-S" "${_src}")
    list(APPEND _all_source_deps "${_src}")

    # Predict the output header name
    get_filename_component(_base_name "${_src}" NAME_WE)
    set(_gen_hdr "${_reflect_output_dir}/reflect/${target}/${_base_name}.reflected.hpp")
    list(APPEND _all_generated_headers "${_gen_hdr}")
  endforeach()

  set(_include_args "")
  foreach(_dir IN LISTS ARG_INCLUDE_DIRS)
    if(NOT IS_ABSOLUTE "${_dir}")
      set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}")
    endif()
    list(APPEND _include_args "-I" "${_dir}")
  endforeach()

  set(_header_args "")
  foreach(_hdr IN LISTS ARG_PRE_INCLUDE_HEADERS)
    list(APPEND _header_args "-H" "${_hdr}")
  endforeach()

  set(_verbose_flag "")
  if(ARG_VERBOSE)
    set(_verbose_flag "-v")
  endif()

  set(_depfile "${_reflect_output_dir}/zpc_reflect_${target}.d")

  # ---- Scan dirs args (passed to the tool at build time) -----------------
  set(_scan_args "")
  if(ARG_SCAN_DIRS)
    foreach(_scan_dir IN LISTS ARG_SCAN_DIRS)
      if(NOT IS_ABSOLUTE "${_scan_dir}")
        set(_scan_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_scan_dir}")
      endif()
      list(APPEND _scan_args "--scan-dir" "${_scan_dir}")
    endforeach()
  endif()

  # ---- Python bindings ---------------------------------------------------
  set(_py_args "")
  set(_py_binding_cpp "")
  set(_py_wrapper_py "")
  set(_py_lib_name "zpc_reflect_py_${target}")
  if(ARG_PYTHON_LIB_NAME)
    set(_py_lib_name "${ARG_PYTHON_LIB_NAME}")
  endif()
  if(ARG_PYTHON_BINDINGS)
    set(_py_output_dir "${_reflect_output_dir}/python")
    file(MAKE_DIRECTORY "${_py_output_dir}")
    set(_py_binding_cpp "${_py_output_dir}/${target}_py_bindings.cpp")
    set(_py_wrapper_py  "${_py_output_dir}/${target}_reflect.py")
    list(APPEND _py_args
      "--python-bindings"
      "--py-output-dir" "${_py_output_dir}"
      "--py-lib-name" "${_py_lib_name}"
    )
  endif()

  # ---- Custom command: run the reflect tool ------------------------------
  # This command depends on all source files and the tool itself.
  # It produces the generated headers AND the registration source.
  # When any source file is modified, CMake/Ninja will re-run this command.
  # Collect optional Python output files for the custom command.
  set(_py_outputs "")
  if(ARG_PYTHON_BINDINGS)
    list(APPEND _py_outputs "${_py_binding_cpp}" "${_py_wrapper_py}")
  endif()

  add_custom_command(
    OUTPUT
      ${_all_generated_headers}
      ${_reflect_gen_src}
      ${_reflect_stamp}
      ${_py_outputs}
    COMMAND ${_tool_cmd}
      ${_src_args}
      -o "${_reflect_output_dir}"
      --generated-source-path "${_reflect_gen_src}"
      -T "${target}"
      --stdc++ "${ARG_CXX_STANDARD}"
      ${_include_args}
      ${_header_args}
      ${_verbose_flag}
      --depfile "${_depfile}"
      ${_py_args}
      ${_scan_args}
    COMMAND ${CMAKE_COMMAND} -E touch "${_reflect_stamp}"
    DEPENDS
      ${_all_source_deps}
      zpc_reflect_tool
    DEPFILE "${_depfile}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "[zpc_reflect] Generating reflection for target '${target}' (${CMAKE_CURRENT_SOURCE_DIR})"
    VERBATIM
    COMMAND_EXPAND_LISTS
  )

  # ---- Create a custom target for the generation step --------------------
  set(_gen_target "zpc_reflect_gen_${target}")
  add_custom_target(${_gen_target}
    DEPENDS
      ${_all_generated_headers}
      ${_reflect_gen_src}
      ${_reflect_stamp}
  )

  # ---- Wire into the user's target --------------------------------------
  # Add generated registration source to the target.
  target_sources(${target} PRIVATE "${_reflect_gen_src}")

  # Add generated directory to include path so user code can do:
  #   #include "reflect/<target>/MyFile.reflected.hpp"
  target_include_directories(${target} PRIVATE "${_reflect_output_dir}")

  # Add the reflection runtime headers (from zpc).
  # (Assumes zpc headers are already on the include path via zpcbase/zpc linkage.)

  # Ensure the generation happens before compilation.
  add_dependencies(${target} ${_gen_target})

  list(LENGTH ARG_SOURCES _src_count)
  message(STATUS "[ZpcReflection] Configured reflection for '${target}' — ${_src_count} source(s), output → ${_reflect_output_dir}")

  # ---- Python binding shared library (optional) -------------------------
  if(ARG_PYTHON_BINDINGS AND _py_binding_cpp)
    set(_py_target "${_py_lib_name}")
    add_library(${_py_target} SHARED "${_py_binding_cpp}")
    target_link_libraries(${_py_target} PRIVATE ${target})
    target_include_directories(${_py_target} PRIVATE "${_reflect_output_dir}")
    add_dependencies(${_py_target} ${_gen_target})
    set_target_properties(${_py_target} PROPERTIES
      PREFIX ""                              # no 'lib' prefix on Linux
      POSITION_INDEPENDENT_CODE ON
    )
    message(STATUS "[ZpcReflection] Python bindings library '${_py_target}' for '${target}', wrapper → ${_py_wrapper_py}")
  endif()
endfunction()

# ----------------------------------------------------------------
# zpc_reflect_sources(<file1> [<file2> ...]
#   TARGET <target>
#   [INCLUDE_DIRS <dir1> ...]
#   [PRE_INCLUDE_HEADERS <header1> ...]
#   [CXX_STANDARD <17|20|23>]
#   [VERBOSE]
# )
#
# Alternative syntax — source files first, target as a keyword.
# ----------------------------------------------------------------
function(zpc_reflect_sources)
  cmake_parse_arguments(PARSE_ARGV 0 ARG
    "VERBOSE;PYTHON_BINDINGS"
    "TARGET;CXX_STANDARD;PYTHON_LIB_NAME"
    "SCAN_DIRS;INCLUDE_DIRS;PRE_INCLUDE_HEADERS"
  )

  if(NOT ARG_TARGET)
    message(FATAL_ERROR "[ZpcReflection] zpc_reflect_sources: TARGET is required")
  endif()
  if(NOT ARG_UNPARSED_ARGUMENTS AND NOT ARG_SCAN_DIRS)
    message(FATAL_ERROR "[ZpcReflection] zpc_reflect_sources: no source files or SCAN_DIRS given")
  endif()

  set(_fwd_args "")
  if(ARG_CXX_STANDARD)
    list(APPEND _fwd_args CXX_STANDARD "${ARG_CXX_STANDARD}")
  endif()
  if(ARG_INCLUDE_DIRS)
    list(APPEND _fwd_args INCLUDE_DIRS ${ARG_INCLUDE_DIRS})
  endif()
  if(ARG_PRE_INCLUDE_HEADERS)
    list(APPEND _fwd_args PRE_INCLUDE_HEADERS ${ARG_PRE_INCLUDE_HEADERS})
  endif()
  if(ARG_VERBOSE)
    list(APPEND _fwd_args VERBOSE)
  endif()
  if(ARG_PYTHON_BINDINGS)
    list(APPEND _fwd_args PYTHON_BINDINGS)
  endif()
  if(ARG_PYTHON_LIB_NAME)
    list(APPEND _fwd_args PYTHON_LIB_NAME "${ARG_PYTHON_LIB_NAME}")
  endif()
  if(ARG_SCAN_DIRS)
    list(APPEND _fwd_args SCAN_DIRS ${ARG_SCAN_DIRS})
  endif()

  # If sources were given positionally, pass them; otherwise rely on SCAN_DIRS
  set(_sources "")
  if(ARG_UNPARSED_ARGUMENTS)
    set(_sources ${ARG_UNPARSED_ARGUMENTS})
  endif()

  zpc_target_reflect_sources(${ARG_TARGET}
    SOURCES ${_sources}
    ${_fwd_args}
  )
endfunction()
