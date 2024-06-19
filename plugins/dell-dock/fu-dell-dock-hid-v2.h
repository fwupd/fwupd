/*
 * Copyright 2024 Realtek Semiconductor Corporation
 * Copyright 2024 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define HID_v2_CMD_WRITE_DATA  0xAB
#define HID_v2_EXT_WRITE_DATA  0x80
#define HID_v2_SUBCMD_FWUPDATE 0x00
#define HID_v2_DATA_PAGE_SZ    192
#define HID_v2_RESPONSE_LENGTH 0x03

gboolean
fu_dell_dock_hid_v2_write(FuDevice *device, GBytes *buf, GError **error);

gboolean
fu_dell_dock_hid_v2_read(FuDevice *device, GByteArray *res, GError **error);

GBytes *
fu_dell_dock_hid_v2_fwup_pkg_new(GBytes *fw, guint8 dev_type, guint8 dev_identifier);
