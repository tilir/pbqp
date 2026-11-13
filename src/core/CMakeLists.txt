# Explicit PBQP core implementation sources.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_COMPONENT_MODULES
  failure.cppm
  ownership.cppm
  problem.cppm
  evaluation.cppm
)
list(TRANSFORM PBQP_COMPONENT_MODULES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(APPEND PBQP_LIBRARY_MODULES ${PBQP_COMPONENT_MODULES})
pbqp_register_format_sources(${PBQP_COMPONENT_MODULES})

set(PBQP_LIBRARY_SOURCES "${PBQP_LIBRARY_SOURCES}" PARENT_SCOPE)
set(PBQP_LIBRARY_MODULES "${PBQP_LIBRARY_MODULES}" PARENT_SCOPE)
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
