/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#include "fu-altos-device.h"

static void
fu_altos_tool_progress_cb (FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	g_print ("Written %u%%\n", fu_device_get_progress (device));
}

int
main (int argc, char **argv)
{
	gsize len;
	g_autofree guint8 *data = NULL;
	g_autoptr(FuAltosDevice) dev = NULL;
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
		GUsbDevice *usb_dev_tmp = g_ptr_array_index (devices, i);
		g_autoptr(FuAltosDevice) dev_tmp = fu_altos_device_new (usb_dev_tmp);
		if (dev_tmp == NULL)
			continue;
		if (fu_altos_device_get_kind (dev_tmp) == FU_ALTOS_DEVICE_KIND_BOOTLOADER) {
			dev = g_object_ref (dev_tmp);
			break;
		}
	}

	/* nothing supported */
	if (dev == NULL) {
		g_print ("No supported device plugged in!\n");
		return 1;
	}
	g_debug ("found %s",
		 fu_altos_device_kind_to_string (fu_altos_device_get_kind (dev)));

	/* open device */
	if (!fu_altos_device_probe (dev, &error)) {
		g_print ("Failed to probe device: %s\n", error->message);
		return 1;
	}
	g_print ("Device Firmware Ver: %s\n", fu_device_get_version (FU_DEVICE (dev)));

	/* load firmware file */
	if (!g_file_get_contents (argv[1], (gchar **) &data, &len, &error)) {
		g_print ("Failed to load file: %s\n", error->message);
		return 1;
	}

	/* update with data blob */
	fw = g_bytes_new (data, len);
	g_signal_connect (dev, "notify::progress",
			  G_CALLBACK (fu_altos_tool_progress_cb), NULL);
	if (!fu_altos_device_write_firmware (dev, fw,
					     FU_ALTOS_DEVICE_WRITE_FIRMWARE_FLAG_NONE,
					     &error)) {
		g_print ("Failed to write firmware: %s\n", error->message);
		return 1;
	}

	return 0;
}
