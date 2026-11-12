# C API library and its private C++ module implementation partitions.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_LIBRARY_SOURCES
  problem_api.cpp
  io.cpp
  bruteforce.cpp
  branch_and_bound.cpp
  parallel.cpp
  greedy.cpp
  reduction.cpp
  solution.cpp)
set(PBQP_LIBRARY_MODULES
  pbqp.cppm
  api_boundary.cppm
  error.cppm
  failure.cppm
  ownership.cppm
  problem.cppm
  text_reader.cppm
  text_writer.cppm
  evaluation.cppm
  bruteforce.cppm
  bruteforce_parallel.cppm
  bnb_bounds.cppm
  bnb_incumbent.cppm
  search_progress.cppm
  search_tasks.cppm
  parallel.cppm
  bnb_tasks.cppm
  bnb_worker.cppm
  branch_and_bound.cppm
  greedy.cppm
  reduction_graph.cppm
  reduction_worklist.cppm
  reduction.cppm
  progress.cppm)
find_package(TBB CONFIG REQUIRED COMPONENTS tbb)

add_library(pbqp ${PBQP_LIBRARY_SOURCES} ${PBQP_PUBLIC_HEADERS})
target_sources(pbqp PRIVATE FILE_SET CXX_MODULES FILES ${PBQP_LIBRARY_MODULES})
target_link_libraries(pbqp PRIVATE TBB::tbb)
target_include_directories(pbqp PUBLIC "${PROJECT_SOURCE_DIR}/include")
pbqp_configure_cxx_target(pbqp PRIVATE)

pbqp_register_format_sources(${PBQP_LIBRARY_SOURCES} ${PBQP_LIBRARY_MODULES})
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
