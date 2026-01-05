/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-logitech-hidpp-struct.h"

#define FU_TYPE_LOGITECH_HIDPP_BOOTLOADER (fu_logitech_hidpp_bootloader_get_type())
G_DECLARE_DERIVABLE_TYPE(FuLogitechHidppBootloader,
			 fu_logitech_hidpp_bootloader,
			 FU,
			 LOGITECH_HIDPP_BOOTLOADER,
			 FuHidDevice)

struct _FuLogitechHidppBootloaderClass {
	FuHidDeviceClass parent_class;
};

#define FU_LOGITECH_HIDPP_BOOTLOADER_FLAG_IS_SIGNED "is-signed"

GPtrArray *
fu_logitech_hidpp_bootloader_parse_pkts(FuLogitechHidppBootloader *self,
					GPtrArray *records,
					GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
FuStructLogitechHidppBootloaderPkt *
fu_logitech_hidpp_bootloader_request(FuLogitechHidppBootloader *self,
				     FuStructLogitechHidppBootloaderPkt *st_req,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);

guint16
fu_logitech_hidpp_bootloader_get_addr_lo(FuLogitechHidppBootloader *self) G_GNUC_NON_NULL(1);
guint16
fu_logitech_hidpp_bootloader_get_addr_hi(FuLogitechHidppBootloader *self) G_GNUC_NON_NULL(1);
guint16
fu_logitech_hidpp_bootloader_get_blocksize(FuLogitechHidppBootloader *self) G_GNUC_NON_NULL(1);
