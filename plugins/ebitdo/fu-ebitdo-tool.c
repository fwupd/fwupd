/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <gusb.h>

#include "fu-ebitdo-common.h"
#include "fu-ebitdo-device.h"

static void
fu_ebitdo_write_progress_cb (goffset current, goffset total, gpointer user_data)
{
	gdouble percentage = -1.f;
	if (total > 0)
		percentage = (100.f * (gdouble) current) / (gdouble) total;
	g_print ("Written %" G_GOFFSET_FORMAT "/%" G_GOFFSET_FORMAT " bytes [%.1f%%]\n",
		 current, total, percentage);
}

int
main (int argc, char **argv)
{
	gsize len;
	g_autofree guint8 *data = NULL;
	g_autoptr(FuEbitdoDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;

	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* require filename */
	if (argc != 2) {
		g_print ("USAGE: %s <filename>\n", argv[0]);
		return 1;
	}

	/* get the device */
	usb_ctx = g_usb_context_new (&error);
	if (usb_ctx == NULL) {
		g_print ("Failed to open USB devices: %s\n", error->message);
		return 1;
	}
	g_usb_context_enumerate (usb_ctx);
	devices = g_usb_context_get_devices (usb_ctx);
	for (guint i = 0; i < devices->len; i++) {
		GUsbDevice *usb_device = g_ptr_array_index (devices, i);
		g_autoptr(FuEbitdoDevice) dev_tmp = fu_ebitdo_device_new (usb_device);
		if (fu_usb_device_probe (FU_USB_DEVICE (dev_tmp), NULL)) {
			dev = g_steal_pointer (&dev_tmp);
			break;
		}
	}

	/* nothing supported */
	if (dev == NULL) {
		g_print ("No supported device plugged in!\n");
		return 1;
	}

	/* open device */
	locker = fu_device_locker_new (dev, &error);
	if (locker == NULL) {
		g_print ("Failed to open USB device: %s\n", error->message);
		return 1;
	}
	g_print ("Device Firmware Ver: %s\n",
		 fu_device_get_version (FU_DEVICE (dev)));
	g_print ("Device Verification ID:\n");
	for (guint i = 0; i < 9; i++)
		g_print ("\t%u = 0x%08x\n", i, fu_ebitdo_device_get_serial(dev)[i]);

	/* not in bootloader mode, so print what to do */
	if (fu_device_has_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER)) {
		g_print ("1. Disconnect the controller\n");
		switch (fu_ebitdo_device_get_kind (dev)) {
		case FU_EBITDO_DEVICE_KIND_FC30:
		case FU_EBITDO_DEVICE_KIND_NES30:
		case FU_EBITDO_DEVICE_KIND_SFC30:
		case FU_EBITDO_DEVICE_KIND_SNES30:
			g_print ("2. Hold down L+R+START for 3 seconds until "
				 "both LED lights flashing.\n");
			break;
		case FU_EBITDO_DEVICE_KIND_FC30PRO:
		case FU_EBITDO_DEVICE_KIND_NES30PRO:
			g_print ("2. Hold down RETURN+POWER for 3 seconds until "
				 "both LED lights flashing.\n");
			break;
		case FU_EBITDO_DEVICE_KIND_FC30_ARCADE:
			g_print ("2. Hold down L1+R1+HOME for 3 seconds until "
				 "both blue LED and green LED blink.\n");
			break;
		default:
			g_print ("2. Do what it says in the manual.\n");
			break;
		}
		g_print ("3. Connect controller\n");
		return 1;
	}

	/* load firmware file */
	if (!g_file_get_contents (argv[1], (gchar **) &data, &len, &error)) {
		g_print ("Failed to load file: %s\n", error->message);
		return 1;
	}

	/* update with data blob */
	fw = g_bytes_new (data, len);
	if (!fu_ebitdo_device_write_firmware (dev, fw,
					      fu_ebitdo_write_progress_cb, NULL,
					      &error)) {
		g_print ("Failed to write firmware: %s\n", error->message);
		return 1;
	}

	/* success */
	g_print ("Now turn off the controller with the power button.\n");

	return 0;
}
