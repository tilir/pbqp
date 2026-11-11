# Shared language, module, diagnostic, and output settings for project targets.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

include_guard(GLOBAL)

function(pbqp_set_output_directories target)
  set_target_properties(${target} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
endfunction()

function(pbqp_configure_cxx_target target visibility)
  target_compile_features(${target} ${visibility} cxx_std_23)
  set_target_properties(${target} PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_SCAN_FOR_MODULES ON)
  pbqp_set_output_directories(${target})
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()
