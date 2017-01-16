/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
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
#include <smbios_c/smbios.h>
#include "synapticsmst-device.h"
#include "synapticsmst-common.h"
#include "fu-plugin-dell.h"
#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#define SYNAPTICS_FLASH_MODE_DELAY 2

static gboolean
synapticsmst_common_check_supported_system (GError **error)
{
	gint i;
	guint8 dell_supported = 0;
	gboolean kernel_support = FALSE;
	struct smbios_struct *de_table;

	de_table = smbios_get_next_struct_by_type (0, 0xDE);
	smbios_struct_get_data (de_table, &(dell_supported), 0x00, sizeof(guint8));
	if (dell_supported != 0xDE) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "MST firmware updating not supported by OEM (%x)",
			     dell_supported);
		return FALSE;
	}
	for (i=0; i<MAX_DP_AUX_NODES; i++) {
		if (kernel_support)
			break;
		kernel_support = g_file_test (synapticsmst_device_aux_node_to_string (i),
					      G_FILE_TEST_EXISTS);
	}
	if (!kernel_support) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "MST firmware updating not supported, missing kernel support.");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_add_device (FuPlugin *plugin,
			 SynapticsMSTDevice *device,
			 guint8 aux_node,
			 guint8 layer,
			 guint8 rad,
			 GError **error) {
	g_autoptr(FuDevice) dev = NULL;
	const gchar *board_str = NULL;
	const gchar *guid_str = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *dev_id_str = NULL;

	if (!synapticsmst_device_enumerate_device (device, error)) {
		g_debug ("SynapticsMST: Error enumerating device at %u", aux_node);
		return FALSE;
	}

	board_str = synapticsmst_device_boardID_to_string (synapticsmst_device_get_boardID(device));
	name = g_strdup_printf ("%s with Synaptics [%s]", board_str,
				synapticsmst_device_get_chipID (device));
	guid_str =  synapticsmst_device_get_guid (device);
	/* Store $KIND-$AUXNODE-$LAYER-$RAD as device ID */
	dev_id_str = g_strdup_printf ("MST-%u-%u-%u-%u",
				      synapticsmst_device_get_kind(device),
				      aux_node, layer, rad);

	if (board_str == NULL) {
		g_debug ("SynapticsMST: invalid board ID");
		return FALSE;
	}

	/* create the device */
	dev = fu_device_new ();
	fu_device_set_id (dev, dev_id_str);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_ALLOW_ONLINE);
	fu_device_set_name (dev, name);
	fu_device_set_version (dev, synapticsmst_device_get_version (device));
	fu_device_add_guid (dev, guid_str);

	fu_plugin_device_add (plugin, dev);
	fu_plugin_cache_add (plugin, dev_id_str, dev);
	return TRUE;
}

static gboolean
fu_plugin_synapticsmst_enumerate (FuPlugin *plugin,
				  GError **error)
{
	guint8 i;
	guint8 j;
	guint16 rad = 0;
	guint8 layer = 0;
	const gchar *aux_node = NULL;
	g_autoptr(SynapticsMSTDevice) device = NULL;
	g_autoptr(FuDevice) dev = NULL;

	for (i=0; i<MAX_DP_AUX_NODES; i++) {
		aux_node = synapticsmst_device_aux_node_to_string (i);
		dev = fu_plugin_cache_lookup (plugin, aux_node);

		/* If we open succesfully a device exists here */
		if (synapticsmst_common_open_aux_node (aux_node) > 0) {
			synapticsmst_common_close_aux_node ();

			/* node already exists */
			if (dev != NULL)
				continue;

			/* Add direct devices */
			g_debug ("SynapticsMST: Adding direct device at %s", aux_node);
			device = synapticsmst_device_new (SYNAPTICSMST_DEVICE_KIND_DIRECT, i, 0, 0);
			if (!fu_synaptics_add_device (plugin, device, i, 0, 0,
						      error))
				continue;

			/* Check for cascaded devices */
			if (!synapticsmst_common_open_aux_node (aux_node))
				continue;
			synapticsmst_device_enable_remote_control (device, error);
			for (j=0; j<2; j++) {
				if (synapticsmst_device_scan_cascade_device (device, j)) {
					layer = synapticsmst_device_get_layer (device) + 1;
					rad = synapticsmst_device_get_rad (device) | (j << (2 * (layer - 1)));
					device = synapticsmst_device_new (SYNAPTICSMST_DEVICE_KIND_REMOTE,
									  i, layer, rad);
					g_debug ("SynapticsMST: Adding cascaded device at %s (%d,%d)", aux_node, layer, rad);
					if (!fu_synaptics_add_device (plugin,
								      device,
								      i,
								      layer,
								      rad,
								      error))
						continue;
				}
			}
			synapticsmst_device_disable_remote_control (device, error);
			synapticsmst_common_close_aux_node ();

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
	g_autoptr(SynapticsMSTDevice) device = NULL;
	const gchar *device_id;
	SynapticsMSTDeviceKind kind;
	guint8 aux_node;
	guint8 layer;
	guint8 rad;
	g_auto (GStrv) split = NULL;

	/* extract details to build a new device */
	device_id = fu_device_get_id (dev);
	split = g_strsplit (device_id, "-", -1);
	kind = g_ascii_strtoull (split[1], NULL, 0);
	aux_node = g_ascii_strtoull (split[2], NULL, 0);
	layer = g_ascii_strtoull (split[3], NULL, 0);
	rad = g_ascii_strtoull (split[4], NULL, 0);


	/* sleep to allow device wakeup to complete */
	g_debug ("SynapticsMST: Waiting %d seconds for MST hub wakeup\n",
		 SYNAPTICS_FLASH_MODE_DELAY);
	g_usleep (SYNAPTICS_FLASH_MODE_DELAY * 1000000);

	device = synapticsmst_device_new (kind, aux_node, layer, rad);

	if (!synapticsmst_device_enumerate_device (device, error))
		return FALSE;
	if (synapticsmst_device_boardID_to_string (synapticsmst_device_get_boardID(device)) != NULL) {
		fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
		if (!synapticsmst_device_write_firmware (device, blob_fw,
							 fu_synapticsmst_write_progress_cb,
							 plugin,
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
	if (!synapticsmst_device_enumerate_device (device, error)) {
		return FALSE;
	}
	fu_device_set_version (dev, synapticsmst_device_get_version (device));

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
	/* verify that this is a supported system */
	if (!synapticsmst_common_check_supported_system (error))
		return FALSE;

	/* look for host devices or already plugged in dock devices */
	if (!fu_plugin_synapticsmst_enumerate (plugin, error))
		g_debug ("SynapticsMST: Error enumerating.");
	return TRUE;
}
