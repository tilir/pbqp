# Runnable C, C++, and optional Python API examples.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

option(PBQP_BUILD_EXAMPLES "Build programming examples" OFF)
add_subdirectory(programming)
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
