# Solver implementation partitions, separated by algorithm.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

add_subdirectory(naive)
add_subdirectory(bnb)
add_subdirectory(bnbopt)
add_subdirectory(greedy)
add_subdirectory(reduction)

set(PBQP_LIBRARY_SOURCES "${PBQP_LIBRARY_SOURCES}" PARENT_SCOPE)
set(PBQP_LIBRARY_MODULES "${PBQP_LIBRARY_MODULES}" PARENT_SCOPE)
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
