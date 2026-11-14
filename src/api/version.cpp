/*
 * Runtime C API version for clients loading a separately built library.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp;

extern "C" void pbqp_get_api_version(pbqp_api_version *out) {
  if (out)
    *out = {PBQP_API_VERSION_MAJOR, PBQP_API_VERSION_MINOR,
            PBQP_API_VERSION_PATCH};
}
