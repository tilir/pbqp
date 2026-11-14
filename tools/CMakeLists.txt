# Solver and generator executables with their shared C++ support modules.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

include("${pbqp_SOURCE_DIR}/cmake/GeneratorVersion.cmake")

set(PBQP_COMMAND_LINE_MODULES command_line.cppm)
set(PBQP_GENERATOR_MODULES generator.cppm)
set(PBQP_GENERATOR_PARTITIONS sampling.cppm)
set(PBQP_GENERATOR_SOURCES generator.cpp)
set(PBQP_CLI_SOURCES pbqp.cpp)
set(PBQP_GENERATE_SOURCES generate.cpp)

add_library(pbqp_command_line STATIC)
target_sources(pbqp_command_line PUBLIC FILE_SET CXX_MODULES FILES ${PBQP_COMMAND_LINE_MODULES})
pbqp_configure_cxx_target(pbqp_command_line PUBLIC)

add_library(pbqp_generator STATIC)
target_sources(pbqp_generator PRIVATE ${PBQP_GENERATOR_SOURCES})
target_sources(pbqp_generator PUBLIC FILE_SET CXX_MODULES FILES ${PBQP_GENERATOR_MODULES})
target_sources(pbqp_generator PRIVATE FILE_SET generator_partitions TYPE CXX_MODULES
  FILES ${PBQP_GENERATOR_PARTITIONS})
target_link_libraries(pbqp_generator PUBLIC pbqp)
pbqp_configure_cxx_target(pbqp_generator PUBLIC)

add_executable(pbqp_cli ${PBQP_CLI_SOURCES})
target_link_libraries(pbqp_cli PRIVATE pbqp pbqp_command_line)
set_target_properties(pbqp_cli PROPERTIES OUTPUT_NAME pbqp)
pbqp_configure_cxx_target(pbqp_cli PRIVATE)

add_executable(pbqp_generate ${PBQP_GENERATE_SOURCES})
target_link_libraries(pbqp_generate PRIVATE pbqp_generator pbqp_command_line)
pbqp_configure_cxx_target(pbqp_generate PRIVATE)
pbqp_generator_version(pbqp_generate)

pbqp_register_format_sources(${PBQP_COMMAND_LINE_MODULES} ${PBQP_GENERATOR_MODULES} ${PBQP_GENERATOR_PARTITIONS} ${PBQP_GENERATOR_SOURCES}
  ${PBQP_CLI_SOURCES} ${PBQP_GENERATE_SOURCES} generator_version.hpp.in)
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
