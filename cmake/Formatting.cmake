# LLVM formatting for explicitly registered project sources and interfaces.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

find_program(PBQP_CLANG_FORMAT NAMES clang-format-21)

function(pbqp_register_format_sources)
  foreach(source IN LISTS ARGN)
    cmake_path(ABSOLUTE_PATH source NORMALIZE OUTPUT_VARIABLE absolute_source)
    list(APPEND PBQP_FORMAT_SOURCES "${absolute_source}")
  endforeach()
  set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
endfunction()

function(pbqp_add_format_targets)
  if(NOT PBQP_CLANG_FORMAT)
    return()
  endif()
  list(REMOVE_DUPLICATES PBQP_FORMAT_SOURCES)

  add_custom_target(format
    COMMAND "${PBQP_CLANG_FORMAT}" --style=file -i ${PBQP_FORMAT_SOURCES}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Formatting C/C++ sources with clang-format-21"
    VERBATIM)

  add_custom_target(check-format
    COMMAND "${PBQP_CLANG_FORMAT}" --style=file --dry-run --Werror
      ${PBQP_FORMAT_SOURCES}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Checking LLVM formatting with clang-format-21"
    VERBATIM)
endfunction()
