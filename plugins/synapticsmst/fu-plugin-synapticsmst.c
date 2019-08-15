/*
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include "fu-synapticsmst-device.h"
#include "fu-synapticsmst-common.h"
#include "fu-plugin-vfuncs.h"
#include "fu-device-metadata.h"

#define SYNAPTICS_FLASH_MODE_DELAY 3
#define SYNAPTICS_UPDATE_ENUMERATE_TRIES 3

static gboolean
fu_synapticsmst_check_amdgpu_safe (GError **error)
{
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) lines = NULL;

	if (!g_file_get_contents ("/proc/modules", &buf, &bufsz, error))
		return FALSE;

	lines = g_strsplit (buf, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix (lines[i], "amdgpu ")) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "amdgpu has known issues with synapticsmst");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_synapticsmst_check_supported_system (FuPlugin *plugin, GError **error)
{

	if (g_getenv ("FWUPD_SYNAPTICSMST_FW_DIR") != NULL) {
		g_debug ("Running Synaptics plugin in test mode");
		return TRUE;
	}

	/* See https://github.com/hughsie/fwupd/issues/1121 for more details */
	if (!fu_synapticsmst_check_amdgpu_safe (error))
		return FALSE;

	if (!g_file_test (SYSFS_DRM_DP_AUX, G_FILE_TEST_IS_DIR)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "MST firmware updating not supported, missing kernel support.");
		return FALSE;
	}
	return TRUE;
}

/* creates MST-$str-$BOARDID */
static void
fu_plugin_synapticsmst_create_simple_guid (FuDevice *fu_device,
					   FuSynapticsmstDevice *device,
					   const gchar *str)
{
	guint16 board_id = fu_synapticsmst_device_get_board_id (device);
	g_autofree gchar *devid = g_strdup_printf ("MST-%s-%u", str, board_id);
	fu_device_add_instance_id (fu_device, devid);
}

/* creates MST-$str-$chipid-$BOARDID */
static void
fu_plugin_synapticsmst_create_complex_guid (FuDevice *fu_device,
					    FuSynapticsmstDevice *device,
					    const gchar *device_kind)
{
	const gchar *chip_id_str = fu_synapticsmst_device_get_chip_id_str (device);
	g_autofree gchar *chip_id_down = g_ascii_strdown (chip_id_str, -1);
	g_autofree gchar *tmp = g_strdup_printf ("%s-%s", device_kind, chip_id_down);

	fu_plugin_synapticsmst_create_simple_guid (fu_device, device, tmp);
}

static gboolean
fu_plugin_synapticsmst_lookup_device (FuPlugin *plugin,
				      FuDevice *fu_device,
				      FuSynapticsmstDevice *device,
				      GError **error)
{
	const gchar *board_str;
	const gchar *guid_template;
	guint16 board_id = fu_synapticsmst_device_get_board_id (device);
	const gchar *chip_id_str = fu_synapticsmst_device_get_chip_id_str (device);
	g_autofree gchar *group = NULL;
	g_autofree gchar *name = NULL;

	/* GUIDs used only for test mode */
	if (g_getenv ("FWUPD_SYNAPTICSMST_FW_DIR") != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = g_strdup_printf ("test-%s", chip_id_str);
		fu_plugin_synapticsmst_create_simple_guid (fu_device, device, tmp);
		return TRUE;
	}

	/* set up the device name via quirks */
	group = g_strdup_printf ("SynapticsMSTBoardID=%u", board_id);
	board_str = fu_plugin_lookup_quirk_by_id (plugin, group,
					      FU_QUIRKS_NAME);
	if (board_str == NULL)
		board_str = "Unknown Platform";
	name = g_strdup_printf ("Synaptics %s inside %s", fu_synapticsmst_device_get_chip_id_str (device),
				board_str);
	fu_device_set_name (fu_device, name);

	/* build the GUIDs for the device */
	guid_template = fu_plugin_lookup_quirk_by_id (plugin, group, "DeviceKind");
	/* no quirks defined for this board */
	if (guid_template == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Unknown board_id %u",
			     board_id);
		return FALSE;

	/* this is a host system, use system ID */
	} else if (g_strcmp0 (guid_template, "system") == 0) {
		const gchar *system_type = fu_plugin_get_dmi_value (plugin,
								    FU_HWIDS_KEY_PRODUCT_SKU);
		fu_plugin_synapticsmst_create_simple_guid (fu_device, device,
							   system_type);
	/* docks or something else */
	} else {
		g_auto(GStrv) templates = NULL;
		templates = g_strsplit (guid_template, ",", -1);
		for (guint i = 0; templates[i] != NULL; i++) {
			fu_plugin_synapticsmst_create_complex_guid (fu_device,
								    device,
								    templates[i]);
		}
	}

	return TRUE;
}

static gboolean
fu_plugin_synaptics_add_device (FuPlugin *plugin,
				FuSynapticsmstDevice *device,
				GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	const gchar *kind_str = NULL;
	g_autofree gchar *dev_id_str = NULL;
	g_autofree gchar *layer_str = NULL;
	g_autofree gchar *rad_str = NULL;
	const gchar *aux_node;
	guint8 layer;
	guint16 rad;

	aux_node = fu_synapticsmst_device_get_aux_node (device);
	if (!fu_synapticsmst_device_enumerate_device (device,
						   error)) {
		g_prefix_error (error, "Error enumerating device at %s: ", aux_node);
		return FALSE;
	}

	/* create the device */
	dev = fu_device_new ();
	/* Store $KIND-$AUXNODE-$LAYER-$RAD as device ID */
	layer = fu_synapticsmst_device_get_layer (device);
	rad = fu_synapticsmst_device_get_rad (device);
	kind_str = fu_synapticsmst_mode_to_string (fu_synapticsmst_device_get_kind (device));
	dev_id_str = g_strdup_printf ("MST-%s-%s-%u-%u",
				      kind_str, aux_node, layer, rad);
	fu_device_set_id (dev, dev_id_str);
	fu_device_set_physical_id (dev, aux_node);
	fu_device_set_metadata (dev, "SynapticsMSTKind", kind_str);
	fu_device_set_metadata (dev, "SynapticsMSTAuxNode", aux_node);
	layer_str = g_strdup_printf ("%u", layer);
	fu_device_set_metadata (dev, "SynapticsMSTLayer", layer_str);
	rad_str = g_strdup_printf ("%u", rad);
	fu_device_set_metadata (dev, "SynapticsMSTRad", rad_str);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_vendor (dev, "Synaptics");
	fu_device_set_summary (dev, "Multi-Stream Transport Device");
	fu_device_add_icon (dev, "video-display");
	fu_device_set_version (dev, fu_synapticsmst_device_get_version (device),
			       FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_quirks (dev, fu_plugin_get_quirks (plugin));

	/* create GUIDs and name */
	if (!fu_plugin_synapticsmst_lookup_device (plugin, dev, device, error))
		return FALSE;
	if (!fu_device_setup (dev, error))
		return FALSE;
	fu_plugin_device_add (plugin, dev);
	fu_plugin_cache_add (plugin, dev_id_str, dev);

	/* inhibit the idle sleep of the daemon */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_INHIBITS_IDLE,
			    "SynapticsMST can cause the screen to flash when probing");
	return TRUE;
}

static gboolean
fu_plugin_synaptics_scan_cascade (FuPlugin *plugin,
				  FuSynapticsmstDevice *device,
				  GError **error)
{
	g_autoptr(FuSynapticsmstDevice) cascade_device = NULL;
	FuDevice *fu_dev = NULL;
	const gchar *aux_node;

	aux_node = fu_synapticsmst_device_get_aux_node (device);
	if (!fu_synapticsmst_device_open (device, error)) {
		g_prefix_error (error,
				"failed to open aux node %s again",
				aux_node);
		return FALSE;
	}

	for (guint8 j = 0; j < 2; j++) {
		guint8 layer = fu_synapticsmst_device_get_layer (device) + 1;
		guint16 rad = fu_synapticsmst_device_get_rad (device) | (j << (2 * (layer - 1)));
		g_autofree gchar *dev_id_str = NULL;
		dev_id_str = g_strdup_printf ("MST-REMOTE-%s-%u-%u",
					      aux_node, layer, rad);
		fu_dev = fu_plugin_cache_lookup (plugin, dev_id_str);

		/* run the scan */
		if (!fu_synapticsmst_device_scan_cascade_device (device, error, j))
			return FALSE;

		/* check if cascaded device was found */
		if (!fu_synapticsmst_device_get_cascade (device)) {
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
			cascade_device = fu_synapticsmst_device_new (FU_SYNAPTICSMST_MODE_REMOTE,
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
		g_autoptr(FuSynapticsmstDevice) device = NULL;
		FuDevice *fu_dev = NULL;

		aux_node = g_dir_read_name (dir);
		if (aux_node == NULL)
			break;

		dev_id_str = g_strdup_printf ("MST-DIRECT-%s-0-0", aux_node);
		fu_dev = fu_plugin_cache_lookup (plugin, dev_id_str);

		/* If we open successfully a device exists here */
		device = fu_synapticsmst_device_new (FU_SYNAPTICSMST_MODE_DIRECT, aux_node, 0, 0);
		if (!fu_synapticsmst_device_open (device, &error_local)) {
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
	g_autoptr(FuSynapticsmstDevice) device = NULL;
	FuSynapticsmstMode kind;
	const gchar *aux_node;
	guint8 layer;
	guint8 rad;
	gboolean reboot;
	gboolean install_force;

	/* extract details to build a new device */
	kind = fu_synapticsmst_mode_from_string (fu_device_get_metadata (dev, "SynapticsMSTKind"));
	aux_node = fu_device_get_metadata (dev, "SynapticsMSTAuxNode");
	layer = g_ascii_strtoull (fu_device_get_metadata (dev, "SynapticsMSTLayer"), NULL, 0);
	rad = g_ascii_strtoull (fu_device_get_metadata (dev, "SynapticsMSTRad"), NULL, 0);


	/* sleep to allow device wakeup to complete */
	g_debug ("waiting %d seconds for MST hub wakeup",
		 SYNAPTICS_FLASH_MODE_DELAY);
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_BUSY);
	g_usleep (SYNAPTICS_FLASH_MODE_DELAY * 1000000);

	device = fu_synapticsmst_device_new (kind, aux_node, layer, rad);

	if (!fu_synapticsmst_device_enumerate_device (device, error))
		return FALSE;
	reboot = !fu_device_has_custom_flag (dev, "skip-restart");
	install_force = (flags & FWUPD_INSTALL_FLAG_FORCE) != 0 ||
			fu_device_has_custom_flag (dev, "ignore-board-id");
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_synapticsmst_device_write_firmware (device, blob_fw,
						 fu_synapticsmst_write_progress_cb,
						 dev,
						 reboot,
						 install_force,
						 error)) {
		g_prefix_error (error, "failed to flash firmware: ");
		return FALSE;
	}

	if (!reboot) {
		g_debug ("Skipping device restart per quirk request");
		return TRUE;
	}

	/* Re-run device enumeration to find the new device version */
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_RESTART);
	for (guint i = 1; i <= SYNAPTICS_UPDATE_ENUMERATE_TRIES; i++) {
		g_autoptr(GError) error_local = NULL;
		g_usleep (SYNAPTICS_FLASH_MODE_DELAY * 1000000);
		if (!fu_synapticsmst_device_enumerate_device (device,
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
	fu_device_set_version (dev, fu_synapticsmst_device_get_version (device),
			       FWUPD_VERSION_FORMAT_TRIPLET);

	return TRUE;
}

gboolean
fu_plugin_device_removed (FuPlugin *plugin, FuDevice *device, GError **error)
{
	const gchar *aux_node;
	const gchar *kind_str;
	const gchar *layer_str;
	const gchar *rad_str;
	g_autofree gchar *dev_id_str = NULL;

	aux_node = fu_device_get_metadata (device, "SynapticsMSTAuxNode");
	if (aux_node == NULL)
		return TRUE;
	kind_str = fu_device_get_metadata (device, "SynapticsMSTKind");
	if (kind_str == NULL)
		return TRUE;
	layer_str = fu_device_get_metadata (device, "SynapticsMSTLayer");
	if (layer_str == NULL)
		return TRUE;
	rad_str = fu_device_get_metadata (device, "SynapticsMSTRad");
	if (rad_str == NULL)
		return TRUE;
	dev_id_str = g_strdup_printf ("MST-%s-%s-%s-%s",
					kind_str, aux_node, layer_str, rad_str);
	if (fu_plugin_cache_lookup (plugin, dev_id_str) != NULL) {
		g_debug ("Removing %s from cache", dev_id_str);
		fu_plugin_cache_remove (plugin, dev_id_str);
	} else {
		g_debug ("%s constructed but not found in cache", dev_id_str);
	}
	return TRUE;
}

static gboolean
fu_plugin_synapticsmst_coldplug (FuPlugin *plugin, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	/* verify that this is a supported system */
	if (!fu_synapticsmst_check_supported_system (plugin, error))
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
fu_plugin_init (FuPlugin *plugin)
{
	/* make sure dell is already coldplugged */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "dell");
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.synaptics.mst");
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}
