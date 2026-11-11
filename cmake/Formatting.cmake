# LLVM-style formatting targets for project sources and module interfaces.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

find_program(PBQP_CLANG_FORMAT NAMES clang-format-21)
if(PBQP_CLANG_FORMAT)
  file(GLOB_RECURSE PBQP_FORMAT_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.cppm"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/tools/*.cpp"
    "${PROJECT_SOURCE_DIR}/tools/*.cppm"
    "${PROJECT_SOURCE_DIR}/tools/*.hpp"
    "${PROJECT_SOURCE_DIR}/tests/*.c"
    "${PROJECT_SOURCE_DIR}/tests/*.cpp"
    "${PROJECT_SOURCE_DIR}/tests/*.hpp")

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
endif()
