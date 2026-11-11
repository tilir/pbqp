# C API library and its private C++ module implementation partitions.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_LIBRARY_SOURCES
  problem_api.cpp
  io.cpp
  bruteforce.cpp
  greedy.cpp
  solution.cpp)
set(PBQP_LIBRARY_MODULES
  pbqp.cppm
  error.cppm
  failure.cppm
  ownership.cppm
  problem.cppm
  text_reader.cppm
  evaluation.cppm
  greedy.cppm
  progress.cppm)
add_library(pbqp ${PBQP_LIBRARY_SOURCES} ${PBQP_PUBLIC_HEADERS})
target_sources(pbqp PRIVATE FILE_SET CXX_MODULES FILES ${PBQP_LIBRARY_MODULES})
target_include_directories(pbqp PUBLIC "${PROJECT_SOURCE_DIR}/include")
pbqp_configure_cxx_target(pbqp PRIVATE)

pbqp_register_format_sources(${PBQP_LIBRARY_SOURCES} ${PBQP_LIBRARY_MODULES})
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
