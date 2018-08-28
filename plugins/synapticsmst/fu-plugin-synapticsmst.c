/*
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include "synapticsmst-device.h"
#include "synapticsmst-common.h"
#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"
#include "fu-device-metadata.h"

#define SYNAPTICS_FLASH_MODE_DELAY 3
#define SYNAPTICS_UPDATE_ENUMERATE_TRIES 3

struct FuPluginData {
	gchar		*system_type;
};

static gboolean
synapticsmst_common_check_supported_system (FuPlugin *plugin, GError **error)
{

	if (g_getenv ("FWUPD_SYNAPTICSMST_FW_DIR") != NULL) {
		g_debug ("Running Synaptics plugin in test mode");
		return TRUE;
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
	FuPluginData *data = fu_plugin_get_data (plugin);

	g_autoptr(FuDevice) dev = NULL;
	const gchar *kind_str = NULL;
	const gchar *board_str = NULL;
	GPtrArray *guids = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *dev_id_str = NULL;
	g_autofree gchar *layer_str = NULL;
	g_autofree gchar *rad_str = NULL;
	const gchar *aux_node;
	guint8 layer;
	guint16 rad;

	aux_node = synapticsmst_device_get_aux_node (device);
	if (!synapticsmst_device_enumerate_device (device,
						   data->system_type,
						   error)) {
		g_prefix_error (error, "Error enumerating device at %s: ", aux_node);
		return FALSE;
	}

	layer = synapticsmst_device_get_layer (device);
	rad = synapticsmst_device_get_rad (device);
	board_str = synapticsmst_device_board_id_to_string (synapticsmst_device_get_board_id (device));
	name = g_strdup_printf ("Synaptics %s inside %s", synapticsmst_device_get_chip_id_str (device),
				board_str);
	guids = synapticsmst_device_get_guids (device);
	if (guids->len == 0) {
		g_debug ("No GUIDs found for board ID %x",
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
	layer_str = g_strdup_printf ("%u", layer);
	rad_str = g_strdup_printf ("%u", rad);

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
	fu_device_set_metadata (dev, "SynapticsMSTKind", kind_str);
	fu_device_set_metadata (dev, "SynapticsMSTAuxNode", aux_node);
	fu_device_set_metadata (dev, "SynapticsMSTLayer", layer_str);
	fu_device_set_metadata (dev, "SynapticsMSTRad", rad_str);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_name (dev, name);
	fu_device_set_vendor (dev, "Synaptics");
	fu_device_set_summary (dev, "Multi-Stream Transport Device");
	fu_device_add_icon (dev, "computer");
	fu_device_set_version (dev, synapticsmst_device_get_version (device));
	fu_device_set_quirks (dev, fu_plugin_get_quirks (plugin));
	for (guint i = 0; i < guids->len; i++)
		fu_device_add_guid (dev, g_ptr_array_index (guids, i));

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
	FuDevice *fu_dev = NULL;
	const gchar *aux_node;

	aux_node = synapticsmst_device_get_aux_node (device);
	if (!synapticsmst_device_open (device, error)) {
		g_prefix_error (error,
				"failed to open aux node %s again",
				aux_node);
		return FALSE;
	}

	for (guint8 j = 0; j < 2; j++) {
		guint8 layer = synapticsmst_device_get_layer (device) + 1;
		guint16 rad = synapticsmst_device_get_rad (device) | (j << (2 * (layer - 1)));
		g_autofree gchar *dev_id_str = NULL;
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
	FuDevice *fu_dev = NULL;

	for (guint8 i = 0; i < 8; i++) {
		for (guint16 j = 0; j < 256; j++) {
			g_autofree gchar *dev_id_str = NULL;
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

	dp_aux_dir = g_getenv ("FWUPD_SYNAPTICSMST_FW_DIR");
	if (dp_aux_dir == NULL)
		dp_aux_dir = SYSFS_DRM_DP_AUX;
	else
		g_debug ("Using %s to look for MST devices", dp_aux_dir);
	dir = g_dir_open (dp_aux_dir, 0, NULL);
	do {
		g_autofree gchar *dev_id_str = NULL;
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
		if (!synapticsmst_device_open (device, &error_local)) {
			/* No device exists here, but was there - remove from DB */
			if (fu_dev != NULL) {
				g_debug ("Removing devices on %s", aux_node);
				fu_plugin_device_remove (plugin, fu_dev);
				fu_plugin_cache_remove (plugin, dev_id_str);
				fu_plugin_synapticsmst_remove_cascaded (plugin,
									aux_node);
			} else {
				/* Nothing to see here - move on*/
				g_debug ("No device found on %s: %s", aux_node, error_local->message);
				g_clear_error (&error_local);
			}
			continue;
		}

		/* Add direct devices */
		if (fu_dev == NULL) {
			g_debug ("Adding direct device %s", dev_id_str);
			if (!fu_plugin_synaptics_add_device (plugin, device, &error_local))
				g_debug ("failed to add device: %s", error_local->message);
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
	FuDevice *device = FU_DEVICE (user_data);
	fu_device_set_progress_full (device, current, total);
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *dev,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(SynapticsMSTDevice) device = NULL;
	SynapticsMSTDeviceKind kind;
	const gchar *aux_node;
	guint8 layer;
	guint8 rad;

	/* extract details to build a new device */
	kind = synapticsmst_device_kind_from_string (fu_device_get_metadata (dev, "SynapticsMSTKind"));
	aux_node = fu_device_get_metadata (dev, "SynapticsMSTAuxNode");
	layer = g_ascii_strtoull (fu_device_get_metadata (dev, "SynapticsMSTLayer"), NULL, 0);
	rad = g_ascii_strtoull (fu_device_get_metadata (dev, "SynapticsMSTRad"), NULL, 0);


	/* sleep to allow device wakeup to complete */
	g_debug ("waiting %d seconds for MST hub wakeup",
		 SYNAPTICS_FLASH_MODE_DELAY);
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_BUSY);
	g_usleep (SYNAPTICS_FLASH_MODE_DELAY * 1000000);

	device = synapticsmst_device_new (kind, aux_node, layer, rad);

	if (!synapticsmst_device_enumerate_device (device,
						   data->system_type, error))
		return FALSE;
	if (synapticsmst_device_board_id_to_string (synapticsmst_device_get_board_id (device)) != NULL) {
		fu_device_set_status (dev, FWUPD_STATUS_DEVICE_WRITE);
		if (!synapticsmst_device_write_firmware (device, blob_fw,
							 fu_synapticsmst_write_progress_cb,
							 dev,
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
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_RESTART);
	for (guint i = 1; i <= SYNAPTICS_UPDATE_ENUMERATE_TRIES; i++) {
		g_autoptr(GError) error_local = NULL;
		g_usleep (SYNAPTICS_FLASH_MODE_DELAY * 1000000);
		if (!synapticsmst_device_enumerate_device (device,
							   data->system_type,
							   &error_local)) {
			g_warning ("Unable to find device after %u seconds: %s",
				   SYNAPTICS_FLASH_MODE_DELAY * i,
				   error_local->message);
			if (i == SYNAPTICS_UPDATE_ENUMERATE_TRIES) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "%s",
					     error_local->message);
				return FALSE;
			}
		}
	}
	fu_device_set_version (dev, synapticsmst_device_get_version (device));

	return TRUE;
}

static gboolean
fu_plugin_synapticsmst_coldplug (FuPlugin *plugin, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	/* verify that this is a supported system */
	if (!synapticsmst_common_check_supported_system (plugin, error))
		return FALSE;

	/* look for host devices or already plugged in dock devices */
	if (!fu_plugin_synapticsmst_enumerate (plugin, &error_local))
		g_debug ("error enumerating: %s", error_local->message);
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	return fu_plugin_synapticsmst_coldplug (plugin, error);
}

gboolean
fu_plugin_recoldplug (FuPlugin *plugin, GError **error)
{
	return fu_plugin_synapticsmst_coldplug (plugin, error);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	g_free(data->system_type);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));

	data->system_type =
		g_strdup (fu_plugin_get_dmi_value (plugin,
						   FU_HWIDS_KEY_PRODUCT_SKU));

	/* make sure dell is already coldplugged */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "dell");
}
