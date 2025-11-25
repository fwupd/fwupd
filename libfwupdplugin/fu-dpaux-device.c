/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuDpauxDevice"

#include "config.h"

#include <unistd.h>

#include "fu-dpaux-device.h"
#include "fu-dpaux-struct.h"
#include "fu-dump.h"
#include "fu-io-channel.h"

/**
 * FuDpauxDevice
 *
 * A Display Port AUX device.
 *
 * See also: #FuUdevDevice
 */

typedef struct {
	guint32 dpcd_ieee_oui;
	guint8 dpcd_hw_rev;
	gchar *dpcd_dev_id;
} FuDpauxDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuDpauxDevice, fu_dpaux_device, FU_TYPE_UDEV_DEVICE)

enum { PROP_0, PROP_DPCD_IEEE_OUI, PROP_LAST };

#define GET_PRIVATE(o) (fu_dpaux_device_get_instance_private(o))

#define FU_DPAUX_DEVICE_READ_TIMEOUT 10 /* ms */

static void
fu_dpaux_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDpauxDevice *self = FU_DPAUX_DEVICE(device);
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_hex(str, idt, "DpcdIeeeOui", priv->dpcd_ieee_oui);
	fwupd_codec_string_append_hex(str, idt, "DpcdHwRev", priv->dpcd_hw_rev);
	fwupd_codec_string_append(str, idt, "DpcdDevId", priv->dpcd_dev_id);
}

static void
fu_dpaux_device_invalidate(FuDevice *device)
{
	FuDpauxDevice *self = FU_DPAUX_DEVICE(device);
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	priv->dpcd_ieee_oui = 0;
	priv->dpcd_hw_rev = 0;
	g_clear_pointer(&priv->dpcd_dev_id, g_free);
}

static gboolean
fu_dpaux_device_probe(FuDevice *device, GError **error)
{
	g_autofree gchar *attr_name = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_dpaux_device_parent_class)->probe(device, error))
		return FALSE;

	/* get from sysfs if not set from tests */
	if (fu_device_get_logical_id(device) == NULL &&
	    fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)) != NULL) {
		g_autofree gchar *logical_id = NULL;
		logical_id =
		    g_path_get_basename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));
		fu_device_set_logical_id(device, logical_id);
	}
	if (fu_device_get_physical_id(device) == NULL) {
		if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device),
						    "pci,drm_dp_aux_dev",
						    error))
			return FALSE;
	}

	/* only populated on real system, test suite won't have udev_device set */
	attr_name = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					      "name",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
	if (attr_name != NULL)
		fu_device_set_name(device, attr_name);

	return TRUE;
}

static gboolean
fu_dpaux_device_setup(FuDevice *device, GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuDpauxDevice *self = FU_DPAUX_DEVICE(device);
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	guint8 buf[FU_STRUCT_DPAUX_DPCD_SIZE] = {0x0};
	g_autoptr(GByteArray) st = NULL;
	/* ignore all Framework FRANDGCP07 BIOS version 3.02 */
	if (fu_device_get_name(device) != NULL &&
	    g_str_has_prefix(fu_device_get_name(device), "AMDGPU DM") &&
	    fu_context_has_hwid_guid(ctx, "32d49d99-414b-55d5-813b-12aaf0335b58")) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "reading %s DPCD is broken on this hardware, "
			    "you need to update the system BIOS",
			    fu_device_get_name(device));
		return FALSE;
	}

	if (!fu_dpaux_device_read(self,
				  FU_DPAUX_DEVICE_DPCD_OFFSET_BRANCH_DEVICE,
				  buf,
				  sizeof(buf),
				  FU_DPAUX_DEVICE_READ_TIMEOUT,
				  error)) {
		g_prefix_error(error, "DPCD read failed: ");
		return FALSE;
	}
	st = fu_struct_dpaux_dpcd_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	g_clear_pointer(&priv->dpcd_dev_id, g_free);
	priv->dpcd_ieee_oui = fu_struct_dpaux_dpcd_get_ieee_oui(st);
	priv->dpcd_hw_rev = fu_struct_dpaux_dpcd_get_hw_rev(st);
	priv->dpcd_dev_id = fu_struct_dpaux_dpcd_get_dev_id(st);
	fu_device_set_version_raw(device, fu_struct_dpaux_dpcd_get_fw_ver(st));

	/* build some extra GUIDs */
	if (priv->dpcd_ieee_oui != 0x0)
		fu_device_add_instance_u32(device, "OUI", priv->dpcd_ieee_oui);
	if (priv->dpcd_hw_rev != 0x0)
		fu_device_add_instance_u8(device, "HWREV", priv->dpcd_hw_rev);
	if (priv->dpcd_dev_id != 0x0)
		fu_device_add_instance_strup(device, "DEVID", priv->dpcd_dev_id);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "DPAUX",
					 "OUI",
					 NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "DPAUX",
					 "OUI",
					 "HWREV",
					 NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "DPAUX",
					 "OUI",
					 "DEVID",
					 NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "DPAUX",
					 "OUI",
					 "HWREV",
					 "DEVID",
					 NULL);

	/* success */
	return TRUE;
}

/**
 * fu_dpaux_device_get_dpcd_ieee_oui:
 * @self: a #FuDpauxDevice
 *
 * Gets the DPCD IEEE OUI.
 *
 * Returns: integer
 *
 * Since: 1.9.8
 **/
guint32
fu_dpaux_device_get_dpcd_ieee_oui(FuDpauxDevice *self)
{
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DPAUX_DEVICE(self), G_MAXUINT32);
	return priv->dpcd_ieee_oui;
}

/**
 * fu_dpaux_device_set_dpcd_ieee_oui:
 * @self: a #FuDpauxDevice
 * @dpcd_ieee_oui: integer
 *
 * Sets the DPCD IEEE OUI.
 *
 * Since: 1.9.8
 **/
void
fu_dpaux_device_set_dpcd_ieee_oui(FuDpauxDevice *self, guint32 dpcd_ieee_oui)
{
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DPAUX_DEVICE(self));
	if (priv->dpcd_ieee_oui == dpcd_ieee_oui)
		return;
	priv->dpcd_ieee_oui = dpcd_ieee_oui;
	g_object_notify(G_OBJECT(self), "dpcd-ieee-oui");
}

/**
 * fu_dpaux_device_get_dpcd_hw_rev:
 * @self: a #FuDpauxDevice
 *
 * Gets the DPCD hardware revision number.
 *
 * Returns: integer
 *
 * Since: 1.9.8
 **/
guint8
fu_dpaux_device_get_dpcd_hw_rev(FuDpauxDevice *self)
{
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DPAUX_DEVICE(self), G_MAXUINT8);
	return priv->dpcd_hw_rev;
}

/**
 * fu_dpaux_device_set_dpcd_hw_rev:
 * @self: a #FuDpauxDevice
 * @dpcd_hw_rev: integer
 *
 * Sets the DPCD hardware revision number.
 *
 * Since: 1.9.8
 **/
void
fu_dpaux_device_set_dpcd_hw_rev(FuDpauxDevice *self, guint8 dpcd_hw_rev)
{
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DPAUX_DEVICE(self));
	priv->dpcd_hw_rev = dpcd_hw_rev;
}

/**
 * fu_dpaux_device_get_dpcd_dev_id:
 * @self: a #FuDpauxDevice
 *
 * Gets the DPCD device ID.
 *
 * Returns: integer
 *
 * Since: 1.9.8
 **/
const gchar *
fu_dpaux_device_get_dpcd_dev_id(FuDpauxDevice *self)
{
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DPAUX_DEVICE(self), NULL);
	return priv->dpcd_dev_id;
}

/**
 * fu_dpaux_device_set_dpcd_dev_id:
 * @self: a #FuDpauxDevice
 * @dpcd_dev_id: integer
 *
 * Sets the DPCD device ID.
 *
 * Since: 1.9.8
 **/
void
fu_dpaux_device_set_dpcd_dev_id(FuDpauxDevice *self, const gchar *dpcd_dev_id)
{
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DPAUX_DEVICE(self));
	if (g_strcmp0(priv->dpcd_dev_id, dpcd_dev_id) == 0)
		return;
	g_free(priv->dpcd_dev_id);
	priv->dpcd_dev_id = g_strdup(dpcd_dev_id);
}

/**
 * fu_dpaux_device_write:
 * @self: a #FuDpauxDevice
 * @buf: (out): data
 * @bufsz: size of @data
 * @error: (nullable): optional return location for an error
 *
 * Write multiple bytes to the DP AUX device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.8
 **/
gboolean
fu_dpaux_device_write(FuDpauxDevice *self,
		      goffset offset,
		      const guint8 *buf,
		      gsize bufsz,
		      guint timeout_ms,
		      GError **error)
{
	g_autofree gchar *title = g_strdup_printf("DPAUX write @0x%x", (guint)offset);

	g_return_val_if_fail(FU_IS_DPAUX_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* seek, then write */
	fu_dump_raw(G_LOG_DOMAIN, title, buf, bufsz);
	if (!fu_udev_device_seek(FU_UDEV_DEVICE(self), offset, error))
		return FALSE;
	return fu_udev_device_write(FU_UDEV_DEVICE(self),
				    buf,
				    bufsz,
				    timeout_ms,
				    FU_IO_CHANNEL_FLAG_NONE,
				    error);
}

/**
 * fu_dpaux_device_read:
 * @self: a #FuDpauxDevice
 * @buf: (out): data
 * @bufsz: size of @data
 * @error: (nullable): optional return location for an error
 *
 * Read multiple bytes from the DP AUX device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.8
 **/
gboolean
fu_dpaux_device_read(FuDpauxDevice *self,
		     goffset offset,
		     guint8 *buf,
		     gsize bufsz,
		     guint timeout_ms,
		     GError **error)
{
	g_autofree gchar *title = g_strdup_printf("DPAUX read @0x%x", (guint)offset);

	g_return_val_if_fail(FU_IS_DPAUX_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* seek, then read */
	if (!fu_udev_device_seek(FU_UDEV_DEVICE(self), offset, error))
		return FALSE;
	if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
				 buf,
				 bufsz,
				 NULL,
				 timeout_ms,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, title, buf, bufsz);

	/* success */
	return TRUE;
}

static void
fu_dpaux_device_incorporate(FuDevice *device, FuDevice *donor)
{
	FuDpauxDevice *self = FU_DPAUX_DEVICE(device);
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	FuDpauxDevicePrivate *priv_donor = GET_PRIVATE(FU_DPAUX_DEVICE(donor));

	g_return_if_fail(FU_IS_DPAUX_DEVICE(self));
	g_return_if_fail(FU_IS_DPAUX_DEVICE(donor));

	/* copy private instance data */
	priv->dpcd_ieee_oui = priv_donor->dpcd_ieee_oui;
	priv->dpcd_hw_rev = priv_donor->dpcd_hw_rev;
	fu_dpaux_device_set_dpcd_dev_id(self,
					fu_dpaux_device_get_dpcd_dev_id(FU_DPAUX_DEVICE(donor)));
}

static gchar *
fu_dpaux_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint24(version_raw, fu_device_get_version_format(device));
}

static void
fu_dpaux_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuDpauxDevice *self = FU_DPAUX_DEVICE(object);
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_DPCD_IEEE_OUI:
		g_value_set_uint(value, priv->dpcd_ieee_oui);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_dpaux_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuDpauxDevice *self = FU_DPAUX_DEVICE(object);
	switch (prop_id) {
	case PROP_DPCD_IEEE_OUI:
		fu_dpaux_device_set_dpcd_ieee_oui(self, g_value_get_uint(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_dpaux_device_init(FuDpauxDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_EMULATED_REQUIRE_SETUP);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_dpaux_device_finalize(GObject *object)
{
	FuDpauxDevice *self = FU_DPAUX_DEVICE(object);
	FuDpauxDevicePrivate *priv = GET_PRIVATE(self);
	g_free(priv->dpcd_dev_id);
	G_OBJECT_CLASS(fu_dpaux_device_parent_class)->finalize(object);
}

static void
fu_dpaux_device_class_init(FuDpauxDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_dpaux_device_finalize;
	object_class->get_property = fu_dpaux_device_get_property;
	object_class->set_property = fu_dpaux_device_set_property;
	device_class->probe = fu_dpaux_device_probe;
	device_class->setup = fu_dpaux_device_setup;
	device_class->invalidate = fu_dpaux_device_invalidate;
	device_class->to_string = fu_dpaux_device_to_string;
	device_class->incorporate = fu_dpaux_device_incorporate;
	device_class->convert_version = fu_dpaux_device_convert_version;

	/**
	 * FuDpauxDevice:dpcd-ieee-oui:
	 *
	 * The DPCD IEEE OUI.
	 *
	 * Since: 1.9.11
	 */
	pspec = g_param_spec_uint("dpcd-ieee-oui",
				  NULL,
				  NULL,
				  0x0,
				  G_MAXUINT32,
				  0x0,
				  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DPCD_IEEE_OUI, pspec);
}
