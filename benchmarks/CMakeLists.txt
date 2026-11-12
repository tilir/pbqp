# Optional reduction scalability measurements, separate from correctness tests.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_REDUCTION_BENCHMARK_SOURCES reduction.cpp)
pbqp_register_format_sources(${PBQP_REDUCTION_BENCHMARK_SOURCES})
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)

option(PBQP_BUILD_BENCHMARKS "Build Google Benchmark scalability benchmarks" OFF)
if(NOT PBQP_BUILD_BENCHMARKS)
  return()
endif()

find_package(benchmark CONFIG REQUIRED)

add_executable(pbqp_benchmark_reduction ${PBQP_REDUCTION_BENCHMARK_SOURCES})
target_link_libraries(pbqp_benchmark_reduction PRIVATE pbqp benchmark::benchmark)
pbqp_configure_cxx_target(pbqp_benchmark_reduction PRIVATE)
