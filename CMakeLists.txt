# Project entry point: shared configuration and component directories.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

cmake_minimum_required(VERSION 3.28)
project(pbqp VERSION 1.0.0 LANGUAGES C CXX)

include(cmake/TargetOptions.cmake)
include(cmake/Formatting.cmake)
include(cmake/Documentation.cmake)
include(CTest)

set(PBQP_FORMAT_SOURCES "")
add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(tools)
add_subdirectory(tests)
add_subdirectory(benchmarks)
add_subdirectory(examples)
pbqp_add_format_targets()
