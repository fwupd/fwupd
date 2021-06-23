/*
 * Copyright (C) 2021, TUXEDO Computers GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ec-common.h"
#include "fu-ec-device.h"

#define FU_PLUGIN_EC_ATTEMPS	100000

typedef struct
{
	gchar			*chipset;
	guint16			 control_port;
	guint16			 data_port;
} FuEcDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuEcDevice, fu_ec_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_ec_device_get_instance_private (o))

enum {
	PROP_0,
	PROP_CHIPSET,
	PROP_LAST
};

static void
fu_ec_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuEcDevice *self = FU_EC_DEVICE (device);
	FuEcDevicePrivate *priv = GET_PRIVATE (self);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS (fu_ec_device_parent_class)->to_string (device, idt, str);

	fu_common_string_append_kv (str, idt, "Chipset", priv->chipset);
	fu_common_string_append_kx (str, idt, "ControlPort", priv->control_port);
	fu_common_string_append_kx (str, idt, "DataPort", priv->data_port);
	fu_common_string_append_ku (str, idt, "AutoloadAction",
				    fu_device_get_metadata_integer (device, "AutoloadAction"));
}

static gboolean
fu_ec_device_wait_for (FuEcDevice *self, guint8 mask, gboolean set, GError **error)
{
	FuEcDevicePrivate *priv = GET_PRIVATE (self);
	for (guint i = 0; i < FU_PLUGIN_EC_ATTEMPS; ++i) {
		guint8 status = 0x00;
		if (!fu_udev_device_pread (FU_UDEV_DEVICE (self), priv->control_port, &status, error))
			return FALSE;
		if (set && (status & mask) != 0)
			return TRUE;
		if (!set && (status & mask) == 0)
			return TRUE;
	}
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_TIMED_OUT,
		     "timed out whilst waiting for 0x%02x:%i", mask, set);
	return FALSE;
}

gboolean
fu_ec_device_write_cmd (FuEcDevice *self, guint8 cmd, GError **error)
{
	FuEcDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_ec_device_wait_for (self, EC_STATUS_IBF, FALSE, error))
		return FALSE;
	return fu_udev_device_pwrite (FU_UDEV_DEVICE (self), priv->control_port, cmd, error);
}

gboolean
fu_ec_device_read (FuEcDevice *self, guint8 *data, GError **error)
{
	FuEcDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_ec_device_wait_for (self, EC_STATUS_OBF, TRUE, error))
		return FALSE;
	return fu_udev_device_pread (FU_UDEV_DEVICE (self), priv->data_port, data, error);
}

gboolean
fu_ec_device_write (FuEcDevice *self, guint8 data, GError **error)
{
	FuEcDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_ec_device_wait_for (self, EC_STATUS_IBF, FALSE, error))
		return FALSE;
	return fu_udev_device_pwrite (FU_UDEV_DEVICE (self), priv->data_port, data, error);
}

gboolean
fu_ec_device_read_reg (FuEcDevice *self, guint8 address, guint8 *data, GError **error)
{
	if (!fu_ec_device_write_cmd (self, EC_CMD_READ, error))
		return FALSE;
	if (!fu_ec_device_write (self, address, error))
		return FALSE;
	return fu_ec_device_read (self, data, error);
}

gboolean
fu_ec_device_write_reg (FuEcDevice *self, guint8 address, guint8 data, GError **error)
{
	if (!fu_ec_device_write_cmd (self, EC_CMD_WRITE, error))
		return FALSE;
	if (!fu_ec_device_write (self, address, error))
		return FALSE;
	return fu_ec_device_write (self, data, error);
}

static gboolean
fu_ec_device_probe (FuDevice *device, GError **error)
{
	FuEcDevice *self = FU_EC_DEVICE (device);
	FuEcDevicePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *devid = NULL;
	g_autofree gchar *name = NULL;

	/* use the chipset name as the logical ID and for the GUID */
	fu_device_set_logical_id (device, priv->chipset);
	devid = g_strdup_printf ("EC-%s", priv->chipset);
	fu_device_add_instance_id (device, devid);
	name = g_strdup_printf ("EC %s", priv->chipset);
	fu_device_set_name (FU_DEVICE (self), name);
	return TRUE;
}

static gboolean
fu_ec_device_setup (FuDevice *device, GError **error)
{
	FuEcDevice *self = FU_EC_DEVICE (device);

	/* sanity check that EC is usable */
	if (!fu_ec_device_wait_for (self, EC_STATUS_IBF, FALSE, error)) {
		g_prefix_error (error, "sanity check: ");
		return FALSE;
	}

	return TRUE;
}

static void
fu_ec_device_get_property (GObject *object, guint prop_id,
			   GValue *value, GParamSpec *pspec)
{
	FuEcDevice *self = FU_EC_DEVICE (object);
	FuEcDevicePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_CHIPSET:
		g_value_set_string (value, priv->chipset);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_ec_device_set_property (GObject *object, guint prop_id,
			   const GValue *value, GParamSpec *pspec)
{
	FuEcDevice *self = FU_EC_DEVICE (object);
	FuEcDevicePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_CHIPSET:
		g_free (priv->chipset);
		priv->chipset = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
fu_ec_device_set_quirk_kv (FuDevice *device,
			   const gchar *key,
			   const gchar *value,
			   GError **error)
{
	FuEcDevice *self = FU_EC_DEVICE (device);
	FuEcDevicePrivate *priv = GET_PRIVATE (self);

	if (g_strcmp0 (key, "EcControlPort") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT16) {
			priv->control_port = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid value");
		return FALSE;
	}
	if (g_strcmp0 (key, "EcDataPort") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT16) {
			priv->data_port = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid value");
		return FALSE;
	}

	/* failed */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_NOT_SUPPORTED,
		     "quirk key %s not supported", key);
	return FALSE;
}

static void
fu_ec_device_init (FuEcDevice *self)
{
	fu_device_set_physical_id (FU_DEVICE (self), "/dev/port");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_protocol (FU_DEVICE (self), "tw.com.ec");
	fu_device_set_summary (FU_DEVICE (self), "Embedded Controller");
	fu_device_add_icon (FU_DEVICE (self), "computer");
}

static void
fu_ec_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_ec_device_parent_class)->finalize (object);
}

static void
fu_ec_device_class_init (FuEcDeviceClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);

	/* properties */
	object_class->get_property = fu_ec_device_get_property;
	object_class->set_property = fu_ec_device_set_property;
	pspec = g_param_spec_string ("chipset", NULL, NULL, NULL,
				     G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_CHIPSET, pspec);

	object_class->finalize = fu_ec_device_finalize;
	klass_device->to_string = fu_ec_device_to_string;
	klass_device->set_quirk_kv = fu_ec_device_set_quirk_kv;
	klass_device->probe = fu_ec_device_probe;
	klass_device->setup = fu_ec_device_setup;
}
