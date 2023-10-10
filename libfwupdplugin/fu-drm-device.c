/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuDrmDevice"

#include "config.h"

#include "fu-bytes.h"
#include "fu-drm-device.h"
#include "fu-string.h"

/**
 * FuDrmDevice
 *
 * A DRM device.
 *
 * See also: #FuUdevDevice
 */

G_DEFINE_TYPE(FuDrmDevice, fu_drm_device, FU_TYPE_UDEV_DEVICE)

static void
fu_drm_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDrmDevice *self = FU_DRM_DEVICE(device);
	if (fu_drm_device_get_connector_id(self) != NULL)
		fu_string_append(str, idt, "ConnectorId", fu_drm_device_get_connector_id(self));
	fu_string_append_kb(str, idt, "Enabled", fu_drm_device_get_enabled(self));
	fu_string_append(str,
			 idt,
			 "Status",
			 fu_display_state_to_string(fu_drm_device_get_state(self)));
}

/**
 * fu_drm_device_get_state:
 * @self: a #FuDrmDevice
 *
 * Gets the current status of the DRM device.
 *
 * Returns: a #FuDisplayState, e.g. %FU_DISPLAY_STATE_CONNECTED
 *
 * Since: 1.9.7
 **/
FuDisplayState
fu_drm_device_get_state(FuDrmDevice *self)
{
	const gchar *tmp;
	g_return_val_if_fail(FU_IS_DRM_DEVICE(self), FU_DISPLAY_STATE_UNKNOWN);
	tmp = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(self), "status", NULL);
	if (g_strcmp0(tmp, "connected") == 0)
		return FU_DISPLAY_STATE_CONNECTED;
	if (g_strcmp0(tmp, "disconnected") == 0)
		return FU_DISPLAY_STATE_DISCONNECTED;
	return FU_DISPLAY_STATE_UNKNOWN;
}

/**
 * fu_drm_device_get_enabled:
 * @self: a #FuDrmDevice
 *
 * Gets if the DRM device is currently enabled.
 *
 * Returns: %TRUE if enabled, %FALSE otherwise
 *
 * Since: 1.9.7
 **/
gboolean
fu_drm_device_get_enabled(FuDrmDevice *self)
{
	const gchar *tmp;
	g_return_val_if_fail(FU_IS_DRM_DEVICE(self), FALSE);
	tmp = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(self), "enabled", NULL);
	return g_strcmp0(tmp, "enabled") == 0;
}

/**
 * fu_drm_device_get_connector_id:
 * @self: a #FuDrmDevice
 *
 * Gets the DRM device connector ID.
 *
 * Returns: a string, or %NULL if not found
 *
 * Since: 1.9.7
 **/
const gchar *
fu_drm_device_get_connector_id(FuDrmDevice *self)
{
	const gchar *tmp;
	g_return_val_if_fail(FU_IS_DRM_DEVICE(self), NULL);
	tmp = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(self), "connector_id", NULL);
	if (tmp == NULL || tmp[0] == '\0')
		return NULL;
	return tmp;
}

/**
 * fu_drm_device_read_edid:
 * @self: a #FuDrmDevice
 * @error: (nullable): optional return location for an error
 *
 * Read the EDID from the DRM device.
 *
 * Returns: (transfer full): a #FuEdid, or %NULL
 *
 * Since: 1.9.7
 **/
FuEdid *
fu_drm_device_read_edid(FuDrmDevice *self, GError **error)
{
	const gchar *sysfs_path;
	g_autofree gchar *edid_path = NULL;
	g_autoptr(FuEdid) edid = fu_edid_new();
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_DRM_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* read blob and parse it */
	sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
	edid_path = g_build_filename(sysfs_path, "edid", NULL);
	blob = fu_bytes_get_contents(edid_path, error);
	if (blob == NULL)
		return NULL;
	if (!fu_firmware_parse(FU_FIRMWARE(edid), blob, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;
	return g_steal_pointer(&edid);
}

static gboolean
fu_drm_device_probe(FuDevice *device, GError **error)
{
	const gchar *sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	g_autofree gchar *physical_id = g_path_get_basename(sysfs_path);

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_drm_device_parent_class)->probe(device, error))
		return FALSE;

	/* this is a heuristic */
	if (physical_id != NULL) {
		g_auto(GStrv) parts = g_strsplit(physical_id, "-", -1);
		for (guint i = 0; parts[i] != NULL; i++) {
			if (g_strcmp0(parts[i], "eDP") == 0)
				fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_drm_device_init(FuDrmDevice *self)
{
}

static void
fu_drm_device_class_init(FuDrmDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_drm_device_probe;
	klass_device->to_string = fu_drm_device_to_string;
}
