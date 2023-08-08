/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_HIDPP_BOOTLOADER (fu_logitech_hidpp_bootloader_get_type())
G_DECLARE_DERIVABLE_TYPE(FuLogitechHidppBootloader,
			 fu_logitech_hidpp_bootloader,
			 FU,
			 LOGITECH_HIDPP_BOOTLOADER,
			 FuHidDevice)

struct _FuLogitechHidppBootloaderClass {
	FuHidDeviceClass parent_class;
};

/**
 * FU_LOGITECH_HIDPP_BOOTLOADER_FLAG_IS_SIGNED:
 *
 * Device requires signed firmware.
 *
 * Since: 1.7.0
 */
#define FU_LOGITECH_HIDPP_BOOTLOADER_FLAG_IS_SIGNED (1 << 0)

/* packet to and from device */
typedef struct __attribute__((packed)) {
	guint8 cmd;
	guint16 addr;
	guint8 len;
	guint8 data[28];
} FuLogitechHidppBootloaderRequest;

FuLogitechHidppBootloaderRequest *
fu_logitech_hidpp_bootloader_request_new(void);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuLogitechHidppBootloaderRequest, g_free);
#pragma clang diagnostic pop

GPtrArray *
fu_logitech_hidpp_bootloader_parse_requests(FuLogitechHidppBootloader *self,
					    GBytes *fw,
					    GError **error);
gboolean
fu_logitech_hidpp_bootloader_request(FuLogitechHidppBootloader *self,
				     FuLogitechHidppBootloaderRequest *req,
				     GError **error);

guint16
fu_logitech_hidpp_bootloader_get_addr_lo(FuLogitechHidppBootloader *self);
guint16
fu_logitech_hidpp_bootloader_get_addr_hi(FuLogitechHidppBootloader *self);
guint16
fu_logitech_hidpp_bootloader_get_blocksize(FuLogitechHidppBootloader *self);
