/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-logitech-hidpp-struct.h"

#define FU_LOGITECH_HIDPP_DEVICE_VID 0x046d

#define FU_LOGITECH_HIDPP_DEVICE_PID_RUNTIME		    0xC52B
#define FU_LOGITECH_HIDPP_DEVICE_PID_BOOTLOADER_NORDIC	    0xAAAA
#define FU_LOGITECH_HIDPP_DEVICE_PID_BOOTLOADER_NORDIC_PICO 0xAAAE
#define FU_LOGITECH_HIDPP_DEVICE_PID_BOOTLOADER_TEXAS	    0xAAAC
#define FU_LOGITECH_HIDPP_DEVICE_PID_BOOTLOADER_TEXAS_PICO  0xAAAD
#define FU_LOGITECH_HIDPP_DEVICE_PID_BOOTLOADER_BOLT	    0xAB07

/* Signed firmware are very long to verify on the device */
#define FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS 30000

/* Polling intervals (ms) */
#define FU_LOGITECH_HIDPP_DEVICE_POLLING_INTERVAL	    30000
#define FU_LOGITECH_HIDPP_RECEIVER_RUNTIME_POLLING_INTERVAL 5000

#define FU_LOGITECH_HIDPP_VERSION_1   0x01
#define FU_LOGITECH_HIDPP_VERSION_BLE 0xFE

/* this is specific to fwupd */
#define FU_LOGITECH_HIDPP_HIDPP_MSG_SW_ID 0x07

gboolean
fu_logitech_hidpp_msg_is_reply(FuStructLogitechHidppMsg *st1,
			       FuStructLogitechHidppMsg *st2,
			       FuLogitechHidppMsgFlags flags) G_GNUC_NON_NULL(1, 2);
gboolean
fu_logitech_hidpp_msg_is_error(FuStructLogitechHidppMsg *st, GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_logitech_hidpp_send(FuUdevDevice *udev_device,
		       FuStructLogitechHidppMsg *st,
		       guint8 hidpp_version,
		       guint timeout,
		       FuLogitechHidppMsgFlags flags,
		       GError **error) G_GNUC_NON_NULL(1, 2);

FuStructLogitechHidppMsg *
fu_logitech_hidpp_receive(FuUdevDevice *udev_device,
			  guint timeout,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
FuStructLogitechHidppMsg *
fu_logitech_hidpp_transfer(FuUdevDevice *udev_device,
			   FuStructLogitechHidppMsg *st,
			   guint8 hidpp_version,
			   FuLogitechHidppMsgFlags flags,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

gchar *
fu_logitech_hidpp_format_version(const gchar *name, guint8 major, guint8 minor, guint16 build)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
