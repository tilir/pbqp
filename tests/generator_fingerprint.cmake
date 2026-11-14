# Verify canonical generator source identities independent of checkout endings.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

include("${SOURCE}/cmake/GeneratorVersion.cmake")
file(MAKE_DIRECTORY "${WORK}/lf" "${WORK}/crlf")
file(WRITE "${WORK}/lf/io.cpp" "first\nsecond\n")
file(WRITE "${WORK}/crlf/io.cpp" "first\r\nsecond\r\n")
pbqp_source_fingerprint(lf "${WORK}/lf" 2.1.0 "${WORK}/lf/io.cpp")
pbqp_source_fingerprint(crlf "${WORK}/crlf" 2.1.0 "${WORK}/crlf/io.cpp")
if(NOT lf STREQUAL crlf)
  message(FATAL_ERROR "Checkout line endings or root changed generator identity")
endif()
file(APPEND "${WORK}/lf/io.cpp" "changed\n")
pbqp_source_fingerprint(edited "${WORK}/lf" 2.1.0 "${WORK}/lf/io.cpp")
if(lf STREQUAL edited)
  message(FATAL_ERROR "Source edit did not change generator identity")
endif()
file(WRITE "${WORK}/lf/new.cppm" "partition\n")
pbqp_source_fingerprint(added "${WORK}/lf" 2.1.0
  "${WORK}/lf/io.cpp" "${WORK}/lf/new.cppm")
pbqp_source_fingerprint(reordered "${WORK}/lf" 2.1.0
  "${WORK}/lf/new.cppm" "${WORK}/lf/io.cpp" "${WORK}/lf/io.cpp")
if(added STREQUAL edited OR NOT added STREQUAL reordered)
  message(FATAL_ERROR "Source collection is incomplete or order-dependent")
endif()
