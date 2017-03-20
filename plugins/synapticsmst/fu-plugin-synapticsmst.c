/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
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
#include <smbios_c/smbios.h>
#include "synapticsmst-device.h"
#include "synapticsmst-common.h"
#include "fu-dell-common.h"
#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#define SYNAPTICS_FLASH_MODE_DELAY 3

static gboolean
synapticsmst_common_check_supported_system (GError **error)
{

	if (g_getenv ("FWUPD_SYNAPTICSMST_FW_DIR") != NULL) {
		g_debug ("Running Synaptics plugin in test mode");
		return TRUE;
	}

	if (!fu_dell_supported ()) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "MST firmware updating not supported by OEM");
		return FALSE;
	}
	if (!g_file_test (SYSFS_DRM_DP_AUX, G_FILE_TEST_IS_DIR)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "MST firmware updating not supported, missing kernel support.");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_synaptics_add_device (FuPlugin *plugin,
				SynapticsMSTDevice *device,
				GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	const gchar *kind_str = NULL;
	const gchar *board_str = NULL;
	const gchar *guid_str = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *dev_id_str = NULL;
	const gchar *aux_node;
	guint8 layer;
	guint16 rad;

	aux_node = synapticsmst_device_get_aux_node (device);
	if (!synapticsmst_device_enumerate_device (device, error)) {
		g_debug ("error enumerating device at %s", aux_node);
		return FALSE;
	}

	layer = synapticsmst_device_get_layer (device);
	rad = synapticsmst_device_get_rad (device);
	board_str = synapticsmst_device_board_id_to_string (synapticsmst_device_get_board_id (device));
	name = g_strdup_printf ("Synaptics %s inside %s", synapticsmst_device_get_chip_id (device),
				board_str);
	guid_str =  synapticsmst_device_get_guid (device);
	if (guid_str == NULL) {
		g_debug ("invalid GUID for board ID %x",
			 synapticsmst_device_get_board_id(device));
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Invalid device");
		return FALSE;
	}
	/* Store $KIND-$AUXNODE-$LAYER-$RAD as device ID */
	kind_str = synapticsmst_device_kind_to_string (synapticsmst_device_get_kind (device));
	dev_id_str = g_strdup_printf ("MST-%s-%s-%u-%u",
				      kind_str, aux_node, layer, rad);

	if (board_str == NULL) {
		g_debug ("invalid board ID (%x)", synapticsmst_device_get_board_id (device));
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Invalid device");
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
fu_plugin_synaptics_scan_cascade (FuPlugin *plugin,
				  SynapticsMSTDevice *device,
				  GError **error)
{
	g_autoptr(SynapticsMSTDevice) cascade_device = NULL;
	g_autofree gchar *dev_id_str = NULL;
	FuDevice *fu_dev = NULL;
	const gchar *aux_node;
	guint8 layer = 0;
	guint16 rad = 0;
	guint8 j;

	aux_node = synapticsmst_device_get_aux_node (device);
	if (!synapticsmst_device_open (device, error)) {
		g_prefix_error (error,
				"failed to open aux node %s again",
				aux_node);
		return FALSE;
	}

	for (j = 0; j < 2; j++) {
		layer = synapticsmst_device_get_layer (device) + 1;
		rad = synapticsmst_device_get_rad (device) | (j << (2 * (layer - 1)));
		dev_id_str = g_strdup_printf ("MST-REMOTE-%s-%u-%u",
					      aux_node, layer, rad);
		fu_dev = fu_plugin_cache_lookup (plugin, dev_id_str);

		/* run the scan */
		if (!synapticsmst_device_scan_cascade_device (device, error, j))
			return FALSE;

		/* check if cascaded device was found */
		if (!synapticsmst_device_get_cascade (device)) {
			/* not found, nothing new to see here, move along */
			if (fu_dev == NULL)
				continue;
			/* not found, but should have existed - remove it */
			else {
				fu_plugin_device_remove (plugin, fu_dev);
				fu_plugin_cache_remove (plugin, dev_id_str);
				/* don't scan any deeper on this node */
				continue;
			}
		/* Found a device, add it */
		} else {
			cascade_device = synapticsmst_device_new (SYNAPTICSMST_DEVICE_KIND_REMOTE,
								  aux_node, layer, rad);
			/* new device */
			if (fu_dev == NULL) {
				g_debug ("Adding remote device %s", dev_id_str);
				if (!fu_plugin_synaptics_add_device (plugin, cascade_device, error))
					return FALSE;
			}
			else
				g_debug ("Skipping previously added device %s",
					 dev_id_str);

			/* check recursively for more devices */
			if (!fu_plugin_synaptics_scan_cascade (plugin, cascade_device, error))
				return FALSE;
		}
	}

	return TRUE;
}

static void
fu_plugin_synapticsmst_remove_cascaded (FuPlugin *plugin, const gchar *aux_node)
{
	g_autofree gchar *dev_id_str = NULL;
	FuDevice *fu_dev = NULL;

	for (guint8 i=0; i < 8; i++) {
		for (guint16 j=0; j < 256; j++) {
			dev_id_str = g_strdup_printf ("MST-REMOTE-%s-%u-%u",
						      aux_node, i, j);
			fu_dev = fu_plugin_cache_lookup (plugin, dev_id_str);
			if (fu_dev != NULL) {
				fu_plugin_device_remove (plugin, fu_dev);
				fu_plugin_cache_remove (plugin, dev_id_str);
				continue;
			}
			break;
		}
	}
}

static gboolean
fu_plugin_synapticsmst_enumerate (FuPlugin *plugin,
				  GError **error)
{
	g_autoptr(GDir) dir = NULL;
	const gchar *dp_aux_dir;
	const gchar *aux_node = NULL;
	g_autofree gchar *dev_id_str = NULL;

	dp_aux_dir = g_getenv ("FWUPD_SYNAPTICSMST_FW_DIR");
	if (dp_aux_dir == NULL)
		dp_aux_dir = SYSFS_DRM_DP_AUX;
	else
		g_debug ("Using %s to look for MST devices", dp_aux_dir);
	dir = g_dir_open (dp_aux_dir, 0, NULL);
	do {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(SynapticsMSTDevice) device = NULL;
		FuDevice *fu_dev = NULL;

		aux_node = g_dir_read_name (dir);
		if (aux_node == NULL)
			break;

		dev_id_str = g_strdup_printf ("MST-DIRECT-%s-0-0", aux_node);
		fu_dev = fu_plugin_cache_lookup (plugin, dev_id_str);

		/* If we open succesfully a device exists here */
		device = synapticsmst_device_new (SYNAPTICSMST_DEVICE_KIND_DIRECT, aux_node, 0, 0);
		if (!synapticsmst_device_open (device, NULL)) {
			/* No device exists here, but was there - remove from DB */
			if (fu_dev != NULL) {
				g_debug ("Removing devices on %s", aux_node);
				fu_plugin_device_remove (plugin, fu_dev);
				fu_plugin_cache_remove (plugin, dev_id_str);
				fu_plugin_synapticsmst_remove_cascaded (plugin,
									aux_node);
			} else {
				/* Nothing to see here - move on*/
				g_debug ("No device found on %s", aux_node);
			}
			continue;
		}

		/* Add direct devices */
		if (fu_dev == NULL) {
			g_debug ("Adding direct device %s", dev_id_str);
			if (!fu_plugin_synaptics_add_device (plugin, device, &error_local))
				g_warning ("failed to add device: %s", error_local->message);
		} else {
			g_debug ("Skipping previously added device %s", dev_id_str);
		}

		/* recursively look for cascade devices */
		if (!fu_plugin_synaptics_scan_cascade (plugin, device, error))
			return FALSE;
	} while(TRUE);
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
	const gchar *aux_node;
	guint8 layer;
	guint8 rad;
	g_auto (GStrv) split = NULL;

	/* extract details to build a new device */
	device_id = fu_device_get_id (dev);
	split = g_strsplit (device_id, "-", -1);
	kind = synapticsmst_device_kind_from_string(split[1]);
	aux_node = split[2];
	layer = g_ascii_strtoull (split[3], NULL, 0);
	rad = g_ascii_strtoull (split[4], NULL, 0);


	/* sleep to allow device wakeup to complete */
	g_debug ("waiting %d seconds for MST hub wakeup",
		 SYNAPTICS_FLASH_MODE_DELAY);
	g_usleep (SYNAPTICS_FLASH_MODE_DELAY * 1000000);

	device = synapticsmst_device_new (kind, aux_node, layer, rad);

	if (!synapticsmst_device_enumerate_device (device, error))
		return FALSE;
	if (synapticsmst_device_board_id_to_string (synapticsmst_device_get_board_id (device)) != NULL) {
		fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
		if (!synapticsmst_device_write_firmware (device, blob_fw,
							 fu_synapticsmst_write_progress_cb,
							 plugin,
							 error)) {
			g_prefix_error (error, "failed to flash firmware: ");
			return FALSE;
		}
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
	if (usb_ctx != NULL) {
		g_signal_connect (usb_ctx, "device-added",
				  G_CALLBACK (fu_plugin_synapticsmst_redo_enumeration_cb),
				  plugin);
		g_signal_connect (usb_ctx, "device-removed",
				  G_CALLBACK (fu_plugin_synapticsmst_redo_enumeration_cb),
				  plugin);
	}
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
		g_debug ("error enumerating");
	return TRUE;
}
