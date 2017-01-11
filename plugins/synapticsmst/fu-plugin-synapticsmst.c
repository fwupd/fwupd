/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
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
#include "synapticsmst-device.h"
#include "synapticsmst-common.h"
#include "fu-plugin-dell.h"
#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#define SYNAPTICS_FLASH_MODE_DELAY 2

static gboolean
fu_plugin_synapticsmst_enumerate (FuPlugin *plugin,
				  GError **error)
{
	guint8 i;
	const gchar *aux_node = NULL;
	const gchar *board_str = NULL;
	g_autofree gchar *name = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(SynapticsMSTDevice) device = NULL;

	if (!synapticsmst_common_check_supported_system (error))
		return FALSE;

	for (i=0; i<MAX_DP_AUX_NODES; i++) {
		aux_node = synapticsmst_device_get_aux_node (i);
		dev = fu_plugin_cache_lookup (plugin, aux_node);

		/* If we open succesfully a device exists here */
		if (synapticsmst_common_open_aux_node (aux_node)) {
			synapticsmst_common_close_aux_node ();

			/* node already exists */
			if (dev != NULL)
				continue;

			g_debug ("SynapticsMST: Adding device at %s", aux_node);
			/* TODO: adding cascading (remote) */

			device = synapticsmst_device_new (SYNAPTICSMST_DEVICE_KIND_DIRECT, aux_node);

			if (!synapticsmst_device_enumerate_device(device, error)) {
				g_debug("SynapticsMST: Error enumerating device at %s", aux_node);
				continue;
			}

			board_str = synapticsmst_device_boardID_to_string(synapticsmst_device_get_boardID(device));
			name = g_strdup_printf ("%s with Synaptics [%s]", board_str,
						synapticsmst_device_get_chipID(device));

			if (board_str == NULL) {
				g_debug ("SynapticsMST: invalid board ID");
				continue;
			}

			/* create the device */
			dev = fu_device_new ();
			fu_device_set_id (dev, aux_node);
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_ALLOW_ONLINE);
			fu_device_set_name (dev, name);
			fu_device_set_version (dev, synapticsmst_device_get_version (device));
			/* GUID is created by board ID */
			fu_device_add_guid (dev, synapticsmst_device_get_guid(device));

			fu_plugin_device_add (plugin, dev);
			fu_plugin_cache_add (plugin, aux_node, dev);
		}
		/* No device exists here, but was there - remove from DB */
		else if (dev != NULL) {
			g_debug ("SynapticsMST: Removing device at %s",
				aux_node);
			fu_plugin_device_remove (plugin, dev);
			fu_plugin_cache_remove (plugin, aux_node);
		} else {
			/* Nothing to see here - move on*/
			g_debug ("SynapticsMST: no device found on %s", aux_node);
		}
	}
	return TRUE;
}

static void
fu_synapticsmst_write_progress_cb (goffset current, goffset total, gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	gdouble percentage = -1.f;
	if (total > 0)
		percentage = (100.f * (gdouble) current) / (gdouble) total;
	g_debug ("written %" G_GOFFSET_FORMAT "/%" G_GOFFSET_FORMAT "[%.1f%%]",
		 current, total, percentage);
	fu_plugin_set_percentage (plugin, (guint) percentage);
}

gboolean
fu_plugin_update_online (FuPlugin *plugin,
			 FuDevice *dev,
			 GBytes *blob_fw,
			 FwupdInstallFlags flags,
			 GError **error)
{
	const gchar *device_id;
	g_autoptr(SynapticsMSTDevice) device = NULL;

	/* sleep to allow device wakeup to complete */
	g_debug ("SynapticsMST: Waiting %d seconds for MST hub wakeup\n",
		 SYNAPTICS_FLASH_MODE_DELAY);
	g_usleep (SYNAPTICS_FLASH_MODE_DELAY * 1000000);

	device_id = fu_device_get_id (dev);
	device = synapticsmst_device_new (SYNAPTICSMST_DEVICE_KIND_DIRECT, device_id);

	if (!synapticsmst_device_enumerate_device (device, error))
		return FALSE;
	if (synapticsmst_device_boardID_to_string (synapticsmst_device_get_boardID(device)) != NULL) {
		fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
		if (!synapticsmst_device_write_firmware (device, blob_fw,
							 fu_synapticsmst_write_progress_cb, plugin,
							 error))
			return FALSE;
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Unknown device");
		return FALSE;
	}

	/* Re-run device enumeration to find the new device version */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_RESTART);
	fu_plugin_device_remove (plugin, dev);
	fu_plugin_cache_remove (plugin, device_id);
	if (!fu_plugin_synapticsmst_enumerate (plugin, error))
		return FALSE;

	return TRUE;
}

static void
fu_plugin_synapticsmst_redo_enumeration_cb (GUsbContext *ctx,
					    GUsbDevice *usb_device,
					    FuPlugin *plugin)
{
	guint16 pid;
	guint16 vid;

	vid = g_usb_device_get_vid (usb_device);
	pid = g_usb_device_get_pid (usb_device);

	/* Only look up if this was a dock connected */
	if (vid != DOCK_NIC_VID || pid != DOCK_NIC_PID)
		return;

	/* Request daemon to redo coldplug, this wakes up Dell devices */
	fu_plugin_recoldplug (plugin);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	g_signal_connect (usb_ctx, "device-added",
			  G_CALLBACK (fu_plugin_synapticsmst_redo_enumeration_cb),
			  plugin);
	g_signal_connect (usb_ctx, "device-removed",
			  G_CALLBACK (fu_plugin_synapticsmst_redo_enumeration_cb),
			  plugin);
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	/* look for host devices or already plugged in dock devices */
	if (!fu_plugin_synapticsmst_enumerate (plugin, error))
		g_debug ("SynapticsMST: Error enumerating.");
	return TRUE;
}
