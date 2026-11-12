/*
 * Primary PBQP implementation module.
 *
 * Internal partitions use namespace pbqp and module linkage without exporting
 * C++ helpers. Only the C API declares project names in the global namespace.
 * The public interface remains the standalone C API in pbqp.h.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
export module pbqp;
