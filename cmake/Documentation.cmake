# Optional public API reference generated directly from the maintained headers.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

option(PBQP_BUILD_DOCS "Enable the Doxygen API reference target" OFF)
if(PBQP_BUILD_DOCS)
  find_package(Doxygen 1.9 REQUIRED COMPONENTS doxygen)
  configure_file("${PROJECT_SOURCE_DIR}/doc/Doxyfile.in"
    "${PROJECT_BINARY_DIR}/docs/Doxyfile" @ONLY)
  add_custom_target(docs
    COMMAND "${DOXYGEN_EXECUTABLE}" "${PROJECT_BINARY_DIR}/docs/Doxyfile"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    DEPENDS "${PROJECT_SOURCE_DIR}/include/pbqp.h"
      "${PROJECT_SOURCE_DIR}/include/pbqp.hpp"
      "${PROJECT_BINARY_DIR}/docs/Doxyfile"
    BYPRODUCTS "${PROJECT_BINARY_DIR}/docs/html/index.html"
    COMMENT "Generating the public C and C++ API reference"
    VERBATIM)
endif()
