# Explicit public C API headers, shared with the library and formatter.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_PUBLIC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/pbqp.h")
pbqp_register_format_sources(${PBQP_PUBLIC_HEADERS})
set(PBQP_PUBLIC_HEADERS "${PBQP_PUBLIC_HEADERS}" PARENT_SCOPE)
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
