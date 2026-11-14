# Content-based generator identity shared by all standard-library builds.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

function(pbqp_generator_version target)
  set(version_sources
    tools/generator.cppm
    tools/generator.cpp
    tools/sampling.cppm
    tools/generate.cpp
    tools/command_line.cppm
    tools/generator_version.hpp.in
    src/io/problem_writer.cppm
    src/io/text_writer.cppm
    include/pbqp.h
    cmake/GeneratorVersion.cmake)
  set(fingerprint_input "${PROJECT_VERSION}\n")
  foreach(source IN LISTS version_sources)
    set(path "${pbqp_SOURCE_DIR}/${source}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(SHA256 "${path}" digest)
    string(APPEND fingerprint_input "${source}:${digest}\n")
  endforeach()
  string(SHA256 PBQP_GENERATOR_FINGERPRINT "${fingerprint_input}")
  set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/generator_version.hpp.in"
    "${generated_dir}/generator_version.hpp" @ONLY)
  target_include_directories(${target} PRIVATE "${generated_dir}")
endfunction()
