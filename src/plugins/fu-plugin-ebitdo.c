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
#include <string.h>

#include "fu-plugin.h"

#include "ebitdo.h"

const gchar *
fu_plugin_get_name (void)
{
	return "ebitdo";
}

gboolean
fu_plugin_device_probe (FuPlugin *plugin, FuDevice *device, GError **error)
{
	const gchar *platform_id;
	g_autoptr(EbitdoDevice) ebitdo_dev = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;

	/* get version */
	platform_id = fu_device_get_id (device);
	usb_ctx = g_usb_context_new (NULL);
	usb_device = g_usb_context_find_by_platform_id (usb_ctx,
							platform_id,
							error);
	if (usb_device == NULL)
		return FALSE;
	ebitdo_dev = ebitdo_device_new (usb_device);
	if (ebitdo_device_get_kind (ebitdo_dev) == EBITDO_DEVICE_KIND_UNKNOWN) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid 0Bitdo device type detected");
		return FALSE;
	}

	/* open the device */
	if (!ebitdo_device_open (ebitdo_dev, error))
		return FALSE;

	/* get the version */
	fu_device_set_version (device, ebitdo_device_get_version (ebitdo_dev));
	g_debug ("overriding the version with %s",
		 ebitdo_device_get_version (ebitdo_dev));

	/* close the device */
	if (!ebitdo_device_close (ebitdo_dev, error))
		return FALSE;

	/* handled in the plugin */
	if (ebitdo_device_get_kind (ebitdo_dev) == EBITDO_DEVICE_KIND_BOOTLOADER)
		fu_device_add_flag (device, FU_DEVICE_FLAG_ALLOW_ONLINE);

	return TRUE;
}

static void
ebitdo_write_progress_cb (goffset current, goffset total, gpointer user_data)
{
	gdouble percentage = -1.f;
	if (total > 0)
		percentage = (100.f * (gdouble) current) / (gdouble) total;
	g_debug ("written %" G_GOFFSET_FORMAT "/%" G_GOFFSET_FORMAT " bytes [%.1f%%]\n",
		 current, total, percentage);
//	fu_provider_set_percentage (provider, (guint) percentage);
}

gboolean
fu_plugin_device_update (FuPlugin *plugin,
			 FuDevice *device,
			 GBytes *data,
			 GError **error)
{
	const gchar *platform_id;
	g_autoptr(EbitdoDevice) ebitdo_dev = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;

	/* get version */
	platform_id = fu_device_get_id (device);
	usb_ctx = g_usb_context_new (NULL);
	usb_device = g_usb_context_find_by_platform_id (usb_ctx,
							platform_id,
							error);
	if (usb_device == NULL)
		return FALSE;
	ebitdo_dev = ebitdo_device_new (usb_device);
	if (ebitdo_device_get_kind (ebitdo_dev) != EBITDO_DEVICE_KIND_BOOTLOADER) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid 0Bitdo device type detected");
		return FALSE;
	}

	/* write the firmware */
	if (!ebitdo_device_open (ebitdo_dev, error))
		return FALSE;
	if (!ebitdo_device_write_firmware (ebitdo_dev, data,
					   ebitdo_write_progress_cb, device,
					   error))
		return FALSE;
	if (!ebitdo_device_close (ebitdo_dev, error))
		return FALSE;

	return TRUE;
}
