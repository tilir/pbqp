# Explicit PBQP api implementation sources.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_COMPONENT_SOURCES
  problem_api.cpp
  io.cpp
  bruteforce.cpp
  branch_and_bound.cpp
  greedy.cpp
  reduction.cpp
  solution.cpp
)
list(TRANSFORM PBQP_COMPONENT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(APPEND PBQP_LIBRARY_SOURCES ${PBQP_COMPONENT_SOURCES})
pbqp_register_format_sources(${PBQP_COMPONENT_SOURCES})
set(PBQP_COMPONENT_MODULES
  api_boundary.cppm
  error.cppm
)
list(TRANSFORM PBQP_COMPONENT_MODULES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(APPEND PBQP_LIBRARY_MODULES ${PBQP_COMPONENT_MODULES})
pbqp_register_format_sources(${PBQP_COMPONENT_MODULES})

set(PBQP_LIBRARY_SOURCES "${PBQP_LIBRARY_SOURCES}" PARENT_SCOPE)
set(PBQP_LIBRARY_MODULES "${PBQP_LIBRARY_MODULES}" PARENT_SCOPE)
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
