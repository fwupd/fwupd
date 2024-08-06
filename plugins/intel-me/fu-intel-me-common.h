/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-intel-me-mkhi-struct.h"

GString *
fu_intel_me_convert_checksum(GByteArray *buf, GError **error);
gboolean
fu_intel_me_mkhi_result_to_error(FuMkhiStatus result, GError **error);
