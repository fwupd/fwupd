/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-vli-struct.h"

gboolean
fu_vli_usbhub_i2c_check_status(FuVliUsbhubI2cStatus status, GError **error);
