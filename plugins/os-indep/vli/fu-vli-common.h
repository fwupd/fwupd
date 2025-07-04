/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-vli-struct.h"

guint32
fu_vli_common_device_kind_get_size(FuVliDeviceKind device_kind);
guint32
fu_vli_common_device_kind_get_offset(FuVliDeviceKind device_kind);
