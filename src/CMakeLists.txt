# C API library assembled from private implementation subsystems.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_LIBRARY_SOURCES "")
set(PBQP_LIBRARY_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/pbqp.cppm")
pbqp_register_format_sources(pbqp.cppm)

add_subdirectory(api)
add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(search)
add_subdirectory(solvers)

find_package(TBB CONFIG REQUIRED COMPONENTS tbb)
add_library(pbqp ${PBQP_LIBRARY_SOURCES} ${PBQP_PUBLIC_HEADERS})
target_sources(pbqp PRIVATE FILE_SET CXX_MODULES FILES ${PBQP_LIBRARY_MODULES})
target_link_libraries(pbqp PRIVATE TBB::tbb)
target_include_directories(pbqp PUBLIC "${PROJECT_SOURCE_DIR}/include")
pbqp_configure_cxx_target(pbqp PRIVATE)

set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
