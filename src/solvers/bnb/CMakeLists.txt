# Unified branch-and-bound engine, term minima, and structural optimizations.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_COMPONENT_MODULES
  branch_and_bound.cppm
  bnb_options.cppm
  bnb_bounds.cppm
  bnb_term_bounds.cppm
  bnb_cover.cppm
  bnb_prefix_pricing.cppm
  bnb_pricing.cppm
  bnb_diagnostics.cppm
  bnb_structure.cppm
  bnb_incumbent.cppm
  bnb_work_queue.cppm
  bnb_tasks.cppm
  bnb_worker.cppm
  bnb_progress.cppm
)
list(TRANSFORM PBQP_COMPONENT_MODULES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(APPEND PBQP_LIBRARY_MODULES ${PBQP_COMPONENT_MODULES})
pbqp_register_format_sources(${PBQP_COMPONENT_MODULES})

set(PBQP_LIBRARY_SOURCES "${PBQP_LIBRARY_SOURCES}" PARENT_SCOPE)
set(PBQP_LIBRARY_MODULES "${PBQP_LIBRARY_MODULES}" PARENT_SCOPE)
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
