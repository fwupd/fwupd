/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuDrmDevice"

#include "config.h"

#include "fu-bytes.h"
#include "fu-common-struct.h"
#include "fu-drm-device.h"
#include "fu-string.h"

/**
 * FuDrmDevice
 *
 * A DRM device.
 *
 * See also: #FuUdevDevice
 */

typedef struct {
	gchar *connector_id;
	gboolean enabled;
	FuDisplayState display_state;
	FuEdid *edid;
} FuDrmDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuDrmDevice, fu_drm_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_drm_device_get_instance_private(o))

static void
fu_drm_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDrmDevice *self = FU_DRM_DEVICE(device);
	FuDrmDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "ConnectorId", priv->connector_id);
	fwupd_codec_string_append_bool(str, idt, "Enabled", priv->enabled);
	fwupd_codec_string_append(str,
				  idt,
				  "State",
				  fu_display_state_to_string(priv->display_state));
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
	FuDrmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DRM_DEVICE(self), FU_DISPLAY_STATE_UNKNOWN);
	return priv->display_state;
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
	FuDrmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DRM_DEVICE(self), FALSE);
	return priv->enabled;
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
	FuDrmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DRM_DEVICE(self), NULL);
	return priv->connector_id;
}

/**
 * fu_drm_device_get_edid:
 * @self: a #FuDrmDevice
 *
 * Returns the cached EDID from the DRM device.
 *
 * Returns: (transfer none): a #FuEdid, or %NULL
 *
 * Since: 1.9.7
 **/
FuEdid *
fu_drm_device_get_edid(FuDrmDevice *self)
{
	FuDrmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DRM_DEVICE(self), NULL);
	return priv->edid;
}

static gboolean
fu_drm_device_probe(FuDevice *device, GError **error)
{
	g_autoptr(FuDevice) parent = NULL;
	FuDrmDevice *self = FU_DRM_DEVICE(device);
	FuDrmDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	g_autofree gchar *attr_enabled = NULL;
	g_autofree gchar *attr_status = NULL;
	g_autofree gchar *attr_connector_id = NULL;
	g_autofree gchar *physical_id = g_path_get_basename(sysfs_path);

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_drm_device_parent_class)->probe(device, error))
		return FALSE;

	/* basic properties */
	attr_enabled = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
						 "enabled",
						 FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						 NULL);
	priv->enabled = g_strcmp0(attr_enabled, "enabled") == 0;
	attr_status = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
						"status",
						FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						NULL);
	priv->display_state = fu_display_state_from_string(attr_status);
	attr_connector_id = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
						      "connector_id",
						      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						      NULL);
	if (attr_connector_id != NULL && attr_connector_id[0] != '\0')
		priv->connector_id = g_strdup(attr_connector_id);

	/* this is a heuristic */
	if (physical_id != NULL) {
		g_auto(GStrv) parts = g_strsplit(physical_id, "-", -1);
		for (guint i = 0; parts[i] != NULL; i++) {
			if (g_strcmp0(parts[i], "eDP") == 0)
				fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
		}
		fu_device_set_physical_id(device, physical_id);
	}

	/* set the parent */
	parent = fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "pci", NULL);
	if (parent != NULL) {
		fu_device_add_parent_backend_id(
		    device,
		    fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(parent)));
	}

	/* read EDID and parse it */
	if (priv->display_state == FU_DISPLAY_STATE_CONNECTED) {
		g_autofree gchar *edid_path = g_build_filename(sysfs_path, "edid", NULL);
		g_autoptr(FuEdid) edid = fu_edid_new();
		g_autoptr(GBytes) blob = NULL;

		blob = fu_bytes_get_contents(edid_path, error);
		if (blob == NULL)
			return FALSE;
		if (!fu_firmware_parse(FU_FIRMWARE(edid), blob, FWUPD_INSTALL_FLAG_NONE, error))
			return FALSE;
		g_set_object(&priv->edid, edid);

		/* add instance ID */
		fu_device_add_instance_str(device, "VEN", fu_edid_get_pnp_id(edid));
		fu_device_add_instance_u16(device, "DEV", fu_edid_get_product_code(edid));
		if (!fu_device_build_instance_id_full(device,
						      FU_DEVICE_INSTANCE_FLAG_GENERIC |
							  FU_DEVICE_INSTANCE_FLAG_VISIBLE |
							  FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						      error,
						      "DRM",
						      "VEN",
						      "DEV",
						      NULL))
			return FALSE;
		if (fu_edid_get_eisa_id(edid) != NULL)
			fu_device_set_name(device, fu_edid_get_eisa_id(edid));
		if (fu_edid_get_serial_number(edid) != NULL)
			fu_device_set_serial(device, fu_edid_get_serial_number(edid));
		fu_device_build_vendor_id(device, "PNP", fu_edid_get_pnp_id(edid));
	}

	/* success */
	return TRUE;
}

static void
fu_drm_device_init(FuDrmDevice *self)
{
}

static void
fu_drm_device_finalize(GObject *object)
{
	FuDrmDevice *self = FU_DRM_DEVICE(object);
	FuDrmDevicePrivate *priv = GET_PRIVATE(self);

	g_free(priv->connector_id);
	if (priv->edid != NULL)
		g_object_unref(priv->edid);

	G_OBJECT_CLASS(fu_drm_device_parent_class)->finalize(object);
}

static void
fu_drm_device_class_init(FuDrmDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_drm_device_finalize;
	device_class->probe = fu_drm_device_probe;
	device_class->to_string = fu_drm_device_to_string;
}
