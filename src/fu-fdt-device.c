/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFdtDevice"

#include "config.h"

#include "fu-fdt-device.h"
#include "fu-fdt-firmware.h"

typedef struct {
	FuFirmware *fdt_firmware;
} FuFdtDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuFdtDevice, fu_fdt_device, FU_TYPE_DEVICE)

enum { PROP_0, PROP_FDT_FIRMWARE, PROP_LAST };

#define GET_PRIVATE(o) (fu_fdt_device_get_instance_private(o))

static gboolean
fu_fdt_device_probe(FuDevice *device, GError **error)
{
	FuFdtDevice *self = FU_FDT_DEVICE(device);
	FuFdtDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuFirmware) fdt_root = NULL;
	g_autofree gchar *compatible = NULL;

	/* set instance ID on device from root "compatible" */
	fdt_root = fu_firmware_get_image_by_id(priv->fdt_firmware, NULL, error);
	if (fdt_root == NULL)
		return FALSE;
	if (!fu_fdt_image_get_attr_str(FU_FDT_IMAGE(fdt_root), "compatible", &compatible, error))
		return FALSE;
	fu_device_add_instance_strsafe(device, "COMPATIBLE", compatible);
	if (!fu_device_build_instance_id_quirk(device, error, "FDT", NULL))
		return FALSE;
	return fu_device_build_instance_id(device, error, "FDT", "COMPATIBLE", NULL);
}

static FuFirmware *
fu_fdt_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFdtDevice *self = FU_FDT_DEVICE(device);
	FuFdtDevicePrivate *priv = GET_PRIVATE(self);
	return g_object_ref(priv->fdt_firmware);
}

static void
fu_fdt_device_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuFdtDevice *self = FU_FDT_DEVICE(obj);
	FuFdtDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_FDT_FIRMWARE:
		g_value_set_object(value, priv->fdt_firmware);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fu_fdt_device_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuFdtDevice *self = FU_FDT_DEVICE(obj);
	FuFdtDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_FDT_FIRMWARE:
		g_set_object(&priv->fdt_firmware, g_value_get_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fu_fdt_device_finalize(GObject *object)
{
	FuFdtDevice *self = FU_FDT_DEVICE(object);
	FuFdtDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->fdt_firmware != NULL)
		g_object_unref(priv->fdt_firmware);
	G_OBJECT_CLASS(fu_fdt_device_parent_class)->finalize(object);
}

static void
fu_fdt_device_init(FuFdtDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
}

static void
fu_fdt_device_class_init(FuFdtDeviceClass *klass)
{
	GParamSpec *pspec;
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_fdt_device_finalize;
	object_class->get_property = fu_fdt_device_get_property;
	object_class->set_property = fu_fdt_device_set_property;

	device_class->probe = fu_fdt_device_probe;
	device_class->read_firmware = fu_fdt_device_read_firmware;

	pspec =
	    g_param_spec_object("fdt-firmware",
				NULL,
				NULL,
				FU_TYPE_FDT_FIRMWARE,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FDT_FIRMWARE, pspec);
}
