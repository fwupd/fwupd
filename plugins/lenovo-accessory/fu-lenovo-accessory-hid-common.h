/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-lenovo-accessory-impl.h"

GByteArray *
fu_lenovo_accessory_hid_read(FuLenovoAccessoryImpl *impl, GError **error);
gboolean
fu_lenovo_accessory_hid_write(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error);
GByteArray *
fu_lenovo_accessory_hid_process(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error);
