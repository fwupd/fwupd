/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

/*
 * Based on the HID++ documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

#define HIDPP_REPORT_NOTIFICATION 0x01

/*
 * Bolt registers
 */
#define BOLT_REGISTER_HIDPP_REPORTING	       0x00
#define BOLT_REGISTER_CONNECTION_STATE	       0x02
#define BOLT_REGISTER_DEVICE_ACTIVITY	       0xB3
#define BOLT_REGISTER_PAIRING_INFORMATION      0xB5
#define BOLT_REGISTER_PERFORM_DEVICE_DISCOVERY 0xC0
#define BOLT_REGISTER_PERFORM_DEVICE_PAIRING   0xC1
#define BOLT_REGISTER_RESET		       0xF2
#define BOLT_REGISTER_RECEIVER_FW_INFORMATION  0xF4
#define BOLT_REGISTER_DFU_CONTROL	       0xF5
#define BOLT_REGISTER_UNIQUE_IDENTIFIER	       0xFB

#include "fu-logitech-hidpp-hidpp-msg.h"

gboolean
fu_logitech_hidpp_send(FuUdevDevice *udev_device,
		       FuLogitechHidppHidppMsg *msg,
		       guint timeout,
		       GError **error);
gboolean
fu_logitech_hidpp_receive(FuUdevDevice *udev_device,
			  FuLogitechHidppHidppMsg *msg,
			  guint timeout,
			  GError **error);
gboolean
fu_logitech_hidpp_transfer(FuUdevDevice *udev_device, FuLogitechHidppHidppMsg *msg, GError **error);
