/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Intel Corporation <thunderbolt-software@lists.01.org>
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

#include <appstream-glib.h>
#include <gudev/gudev.h>
#include <tbt/tbt_fwu.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#define FU_PLUGIN_THUNDERBOLT_MAX_ID_LEN	255
#define FU_PLUGIN_THUNDERBOLT_DAEMON_DELAY	3000	/* ms */

#ifdef HAVE_DELL
#include "fu-dell-common.h"
#include <smbios_c/system_info.h>
#endif /* HAVE_DELL */

struct FuPluginData {
	/* A handle on a list of libtbtfwu controller objects.  These must
	 * eventually be freed.
	 */
	struct tbt_fwu_Controller		**controllers;

	/* The number of controller objects in controllers and in
	 * controller_info.
	 */
	gsize					 controllers_len;

	/* the FuThunderboltInfo objects found using the last rescan */
	GPtrArray				*infos;

	/* the array of sysfs paths */
	GPtrArray				*devpaths;

	/* A handle on some state for dealing with our registration
	 * for udev events.
	 */
	GUdevClient				*gudev_client;

	/* The idle timeout for refresh */
	guint					 refresh_id;
};

typedef struct {
	struct tbt_fwu_Controller		*controller;
	gchar					*id;
	guint16					 model_id;
	guint16					 vendor_id;
	guint32					 version_major;
	guint32					 version_minor;
	FuDevice				*dev;
} FuThunderboltInfo;

static void
fu_plugin_thunderbolt_info_free (FuThunderboltInfo *info)
{
	g_free (info->id);
	if (info->dev != NULL)
		g_object_unref (info->dev);
	g_slice_free (FuThunderboltInfo, info);
}

static FuThunderboltInfo *
fu_plugin_thunderbolt_get_info_by_id (FuPlugin *plugin, const gchar *id)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	for (guint i = 0; i < data->infos->len; i++) {
		FuThunderboltInfo *info = g_ptr_array_index (data->infos, i);
		if (g_strcmp0 (info->id, id) == 0)
			return info;
	}
	return NULL;
}

static gboolean
fu_plugin_thunderbolt_rescan (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gint rc;
	g_autoptr(GPtrArray) infos_remove = NULL;

	/* get the new list */
	if (data->controllers != NULL) {
		tbt_fwu_freeControllerList (data->controllers,
					    data->controllers_len);
	}
	data->controllers = NULL;
	rc = tbt_fwu_getControllerList (&data->controllers,
					&data->controllers_len);
	if (rc != TBT_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to retrieve TBT controller list: %s",
			     tbt_strerror (rc));
		return FALSE;
	}
	g_debug ("found %" G_GSIZE_FORMAT " thunderbolt controllers",
		 data->controllers_len);

	/* no longer valid */
	for (guint i = 0; i < data->infos->len; i++) {
		FuThunderboltInfo *info = g_ptr_array_index (data->infos, i);
		info->controller = NULL;
	}

	/* go through each device in results */
	for (guint i = 0; i < data->controllers_len; i++) {
		FuThunderboltInfo *info;
		gchar tdbid[FU_PLUGIN_THUNDERBOLT_MAX_ID_LEN];
		gsize tdbid_sz = sizeof (tdbid);
		g_autofree gchar *guid_id = NULL;
		g_autofree gchar *version = NULL;
		gint safe_mode = 0;

		/* get the ID */
		rc = tbt_fwu_Controller_getID (data->controllers[i],
					       tdbid, &tdbid_sz);
		if (rc != TBT_OK) {
			g_warning ("failed to get tbd ID: %s",
				   tbt_strerror (rc));
			continue;
		}

		/* find any existing info struct */
		info = fu_plugin_thunderbolt_get_info_by_id (plugin, tdbid);
		if (info != NULL) {
			info->controller = data->controllers[i];
			continue;
		}

		/* create a new info struct */
		info = g_slice_new0 (FuThunderboltInfo);
		info->controller = data->controllers[i];
		info->id = g_strdup (tdbid);
		g_ptr_array_add (data->infos, info);

		rc = tbt_fwu_Controller_isInSafeMode (data->controllers[i], &safe_mode);
		if (rc != TBT_OK) {
			g_warning ("failed to get controller status: %s",
				   tbt_strerror (rc));
			continue;
		}

		if (safe_mode != 0) {
			info->vendor_id = 0;
			info->model_id = 0;
			info->version_major = 0;
			info->version_minor = 0;
			g_warning ("Thunderbolt controller %s is in Safe Mode.  "
				   "Please visit https://github.com/01org/tbtfwupd/wiki "
				   "for information on how to restore normal operation.",
				   info->id);

			/* Dell systems are known to have the system ID as the model_id
			   when in safe mode, they can be flashed */
#ifdef HAVE_DELL
			if (fu_dell_supported ()) {
				info->vendor_id = 0x00d4;
				info->model_id = sysinfo_get_dell_system_id ();
				safe_mode = 0;
			}
#endif /* HAVE_DELL */
		} else {
			/* get the vendor ID */
			rc = tbt_fwu_Controller_getVendorID (data->controllers[i],
							     &info->vendor_id);
			if (rc != TBT_OK) {
				g_warning ("failed to get tbd vendor ID: %s",
					   tbt_strerror (rc));
				continue;
			}

			/* get the model ID */
			rc = tbt_fwu_Controller_getModelID (data->controllers[i],
							    &info->model_id);
			if (rc != TBT_OK) {
				g_warning ("failed to get tbd model ID: %s",
					   tbt_strerror (rc));
				continue;
			}

			/* get the controller info */
			rc = tbt_fwu_Controller_getNVMVersion (data->controllers[i],
							       &info->version_major,
							       &info->version_minor);
			if (rc != TBT_OK) {
				g_warning ("failed to get tbd firmware version: %s",
					   tbt_strerror (rc));
				continue;
			}
		}

		/* add FuDevice attributes */
		info->dev = fu_device_new ();
		fu_device_set_vendor (info->dev, "Intel");
		fu_device_set_name (info->dev, "Thunderbolt Controller");
		fu_device_add_flag (info->dev, FWUPD_DEVICE_FLAG_INTERNAL);
		if (safe_mode == 0)
			fu_device_add_flag (info->dev, FWUPD_DEVICE_FLAG_ALLOW_ONLINE);
		fu_device_set_id (info->dev, info->id);

		/* add GUID that the info firmware uses */
		guid_id = g_strdup_printf ("TBT-%04x%04x",
					   info->vendor_id, info->model_id);
		fu_device_add_guid (info->dev, guid_id);

		/* format version */
		version = g_strdup_printf ("%" G_GINT32_MODIFIER "x.%02" G_GINT32_MODIFIER "x",
					   info->version_major,
					   info->version_minor);
		fu_device_set_version (info->dev, version);

		/* add to daemon */
		fu_plugin_device_add (plugin, info->dev);
	}

	/* any devices were removed */
	infos_remove = g_ptr_array_new ();
	for (guint i = 0; i < data->infos->len; i++) {
		FuThunderboltInfo *info = g_ptr_array_index (data->infos, i);
		if (info->controller == NULL) {
			if (info->dev != NULL)
				fu_plugin_device_remove (plugin, info->dev);
			g_ptr_array_add (infos_remove, info);
		}
	}
	for (guint i = 0; i < infos_remove->len; i++) {
		FuThunderboltInfo *info = g_ptr_array_index (infos_remove, i);
		g_ptr_array_remove (data->infos, info);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_plugin_thunderbolt_schedule_rescan_cb (gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(GError) error = NULL;

	/* no longer valid */
	data->refresh_id = 0;

	/* rescan */
	if (!fu_plugin_thunderbolt_rescan (plugin, &error))
		g_warning ("%s", error->message);
	return FALSE;
}

static void
fu_plugin_thunderbolt_schedule_rescan (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	/* this delay is a work-around for potential race conditions in which
	 * the udev event arrives to fwupd prior to the daemon refreshing */
	if (data->refresh_id != 0)
		g_source_remove (data->refresh_id);
	data->refresh_id = g_timeout_add (FU_PLUGIN_THUNDERBOLT_DAEMON_DELAY,
					  fu_plugin_thunderbolt_schedule_rescan_cb,
					  plugin);
}

static gboolean
fu_plugin_thunderbolt_device_matches (GUdevDevice *device)
{
	guint16 device_id;
	guint16 vendor_id;

	/* check vendor ID */
	vendor_id = g_udev_device_get_sysfs_attr_as_int (device, "vendor");
	if (vendor_id != 0x8086)
		return FALSE;

	/* check device ID */
	device_id = g_udev_device_get_sysfs_attr_as_int (device, "device");
	if (device_id != 0x1577 &&
	    device_id != 0x1575 &&
	    device_id != 0x15BF &&
	    device_id != 0x15D2 &&
	    device_id != 0x15D9)
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_plugin_thunderbolt_percentage_changed_cb (guint percentage, gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	fu_plugin_set_percentage (plugin, percentage);
}

gboolean
fu_plugin_update_online (FuPlugin *plugin,
			 FuDevice *dev,
			 GBytes *blob_fw,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuThunderboltInfo *info;
	const guint8 *blob;
	gint rc;
	gsize blob_sz;

	/* find controller */
	info = fu_plugin_thunderbolt_get_info_by_id (plugin, fu_device_get_id (dev));
	if (info == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no TBT device with ID %s found",
			     fu_device_get_id (dev));
		return FALSE;
	}

	/* validate the image */
	blob = (const guint8 *) g_bytes_get_data (blob_fw, &blob_sz);
	rc = tbt_fwu_Controller_validateFWImage (info->controller, blob, blob_sz);
	if (rc != TBT_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "TBT firmware validation failed: %s",
			     tbt_strerror (rc));
		return FALSE;
	}

	/* update the device */
	rc = tbt_fwu_Controller_updateFW (info->controller,
					  blob,
					  blob_sz,
					  fu_plugin_thunderbolt_percentage_changed_cb,
					  plugin);
	if (rc != TBT_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "TBT firmware update failed: %s",
			     tbt_strerror (rc));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static const gchar *
fu_plugin_thunderbolt_find_devpath (FuPlugin *plugin, GUdevDevice *udev_device)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *devpath = g_udev_device_get_sysfs_path (udev_device);
	for (guint i = 0; i < data->devpaths->len; i++) {
		const gchar *devpath_tmp = g_ptr_array_index (data->devpaths, i);
		if (g_strcmp0 (devpath_tmp, devpath) == 0)
			return devpath_tmp;
	}
	return NULL;
}

static void
fu_plugin_thunderbolt_add_devpath (FuPlugin *plugin, GUdevDevice *udev_device)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	/* already exists */
	if (fu_plugin_thunderbolt_find_devpath (plugin, udev_device) != NULL)
		return;

	/* add new sysfs-path */
	g_ptr_array_add (data->devpaths,
			 g_strdup (g_udev_device_get_sysfs_path (udev_device)));
}

static void
fu_plugin_thunderbolt_uevent_cb (GUdevClient *gudev_client,
				 const gchar *action,
				 GUdevDevice *udev_device,
				 FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (g_strcmp0 (action, "remove") == 0) {
		const gchar *devpath = NULL;
		devpath = fu_plugin_thunderbolt_find_devpath (plugin, udev_device);
		if (devpath != NULL) {
			g_debug ("potentially removing tbt device");
			g_ptr_array_remove (data->devpaths, (gpointer) devpath);
			fu_plugin_thunderbolt_schedule_rescan (plugin);
		}
		return;
	}
	if (g_strcmp0 (action, "add") == 0) {
		if (fu_plugin_thunderbolt_device_matches (udev_device)) {
			g_debug ("potentially adding tbt device");
			fu_plugin_thunderbolt_add_devpath (plugin, udev_device);
			fu_plugin_thunderbolt_schedule_rescan (plugin);
		}
		return;
	}
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	const gchar *subsystems[] = { "pci", NULL };
	data->infos = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_plugin_thunderbolt_info_free);
	data->devpaths = g_ptr_array_new_with_free_func (g_free);
	data->gudev_client = g_udev_client_new (subsystems);
	g_signal_connect (data->gudev_client, "uevent",
			  G_CALLBACK (fu_plugin_thunderbolt_uevent_cb), plugin);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->refresh_id != 0)
		g_source_remove (data->refresh_id);
	g_ptr_array_unref (data->infos);
	g_ptr_array_unref (data->devpaths);
	tbt_fwu_freeControllerList (data->controllers, data->controllers_len);
	tbt_fwu_shutdown ();
	g_object_unref (data->gudev_client);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	gint rc = tbt_fwu_init ();
	if (rc != TBT_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "TBT initialization failed: %s",
			     tbt_strerror (rc));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gboolean found = FALSE;
	g_autoptr(GList) devices = NULL;

	/* get all devices of class */
	devices = g_udev_client_query_by_subsystem (data->gudev_client, "pci");
	for (GList *l = devices; l != NULL; l = l->next) {
		GUdevDevice *udev_device = l->data;
		if (fu_plugin_thunderbolt_device_matches (udev_device)) {
			fu_plugin_thunderbolt_add_devpath (plugin, udev_device);
			found = TRUE;
			break;
		}
	}
	if (found) {
		g_debug ("found thunderbolt PCI device on coldplug");
		if (!fu_plugin_thunderbolt_rescan (plugin, error))
			return FALSE;
	}
	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
	return TRUE;
}
