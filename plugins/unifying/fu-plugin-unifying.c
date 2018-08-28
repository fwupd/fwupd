/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <appstream-glib.h>
#include <fwupd.h>
#include <gusb.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "lu-context.h"
#include "lu-device.h"
#include "lu-device-peripheral.h"

struct FuPluginData {
	LuContext		*ctx;
};

static gboolean
fu_plugin_unifying_device_added (FuPlugin *plugin,
				 LuDevice *device,
				 GError **error)
{
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;

	/* profile */
	ptask = as_profile_start (profile, "FuPluginLu:added{%s}",
				  fu_device_get_platform_id (FU_DEVICE (device)));
	g_assert (ptask != NULL);

	/* open the device */
	if (!lu_device_open (device, error))
		return FALSE;

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	return TRUE;
}

static gboolean
fu_plugin_unifying_detach_cb (gpointer user_data)
{
	FuDevice *device = FU_DEVICE (user_data);
	g_autoptr(GError) error = NULL;

	/* ditch this device */
	g_debug ("detaching");
	if (!fu_device_detach (device, &error)) {
		g_warning ("failed to detach: %s", error->message);
		return FALSE;
	}

	return FALSE;
}

static gboolean
fu_plugin_unifying_attach_cb (gpointer user_data)
{
	FuDevice *device = FU_DEVICE (user_data);
	g_autoptr(GError) error = NULL;

	/* ditch this device */
	g_debug ("attaching");
	if (!fu_device_attach (device, &error)) {
		g_warning ("failed to detach: %s", error->message);
		return FALSE;
	}

	return FALSE;
}

gboolean
fu_plugin_update_detach (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	LuDevice *device = LU_DEVICE (dev);

	/* get device */
	if (!lu_device_open (device, error))
		return FALSE;

	/* switch to bootloader if required */
	if (!lu_device_has_flag (device, LU_DEVICE_FLAG_REQUIRES_DETACH))
		return TRUE;

	/* wait for device to come back */
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_RESTART);
	if (lu_device_has_flag (device, LU_DEVICE_FLAG_DETACH_WILL_REPLUG)) {
		g_debug ("doing detach in idle");
		g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
				 fu_plugin_unifying_detach_cb,
				 g_object_ref (dev),
				 (GDestroyNotify) g_object_unref);
		if (!lu_context_wait_for_replug (data->ctx,
						 device,
						 FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						 error))
			return FALSE;
	} else {
		g_debug ("doing detach in main thread");
		if (!fu_device_detach (dev, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_update_attach (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	LuDevice *device = LU_DEVICE (dev);

	/* get device */
	if (!lu_device_open (device, error))
		return FALSE;

	/* wait for it to appear back in runtime mode if required */
	if (!lu_device_has_flag (device, LU_DEVICE_FLAG_REQUIRES_ATTACH))
		return TRUE;

	/* wait for device to come back */
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_RESTART);
	if (lu_device_has_flag (device, LU_DEVICE_FLAG_ATTACH_WILL_REPLUG)) {
		g_debug ("doing attach in idle");
		g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
				 fu_plugin_unifying_attach_cb,
				 g_object_ref (device),
				 (GDestroyNotify) g_object_unref);
		if (!lu_context_wait_for_replug (data->ctx,
						 device,
						 FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						 error))
			return FALSE;
	} else {
		g_debug ("doing attach in main thread");
		if (!fu_device_attach (dev, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_update_reload (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	LuDevice *device = LU_DEVICE (dev);

	/* get device */
	if (!lu_device_open (device, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *dev,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	LuDevice *device = LU_DEVICE (dev);

	/* get version */
	if (!lu_device_open (device, error))
		return FALSE;

	/* write the firmware */
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_device_write_firmware (dev, blob_fw, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_plugin_unifying_device_added_cb (LuContext *ctx,
				    LuDevice *device,
				    FuPlugin *plugin)
{
	g_autoptr(GError) error = NULL;

	/* add */
	if (!fu_plugin_unifying_device_added (plugin, device, &error)) {
		if (g_error_matches (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED)) {
			g_debug ("Failed to add Logitech device: %s",
				  error->message);
		} else {
			g_warning ("Failed to add Logitech device: %s",
				   error->message);
		}
	}
}

static void
fu_plugin_unifying_device_removed_cb (LuContext *ctx,
				      LuDevice *device,
				      FuPlugin *plugin)
{
	fu_plugin_device_remove (plugin, FU_DEVICE (device));
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	/* check the kernel has CONFIG_HIDRAW */
	if (!g_file_test ("/sys/class/hidraw", G_FILE_TEST_IS_DIR)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no kernel support for CONFIG_HIDRAW");
		return FALSE;
	}

	/* coldplug */
	g_signal_connect (data->ctx, "added",
			  G_CALLBACK (fu_plugin_unifying_device_added_cb),
			  plugin);
	g_signal_connect (data->ctx, "removed",
			  G_CALLBACK (fu_plugin_unifying_device_removed_cb),
			  plugin);
	lu_context_set_supported (data->ctx, fu_plugin_get_supported (plugin));
	return TRUE;
}


gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	lu_context_coldplug (data->ctx);
	lu_context_set_poll_interval (data->ctx, 5000);
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	data->ctx = lu_context_new_full (usb_ctx);
	g_object_set (data->ctx,
		      "system-quirks", fu_plugin_get_quirks (plugin),
		      NULL);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_object_unref (data->ctx);
}
