/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vbe-device.h"

enum { PROP_0, PROP_VBE_METHOD, PROP_FDT_ROOT, PROP_FDT_NODE, PROP_VBE_DIR, PROP_LAST };

typedef struct {
	FuFdtImage *fdt_root;
	FuFdtImage *fdt_node;
	gchar **compatible;
	gchar *vbe_dir;
} FuVbeDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuVbeDevice, fu_vbe_device, FU_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_vbe_device_get_instance_private(o))

static void
fu_vbe_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuVbeDevice *self = FU_VBE_DEVICE(device);
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);

	fu_string_append(str, idt, "VbeDir", priv->vbe_dir);
	if (priv->compatible != NULL) {
		g_autofree gchar *tmp = g_strjoinv(":", priv->compatible);
		fu_string_append(str, idt, "Compatible", tmp);
	}
}

FuFdtImage *
fu_vbe_device_get_fdt_root(FuVbeDevice *self)
{
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_VBE_DEVICE(self), NULL);
	return priv->fdt_root;
}

FuFdtImage *
fu_vbe_device_get_fdt_node(FuVbeDevice *self)
{
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_VBE_DEVICE(self), NULL);
	return priv->fdt_node;
}

gchar **
fu_vbe_device_get_compatible(FuVbeDevice *self)
{
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_VBE_DEVICE(self), NULL);
	return priv->compatible;
}

const gchar *
fu_vbe_device_get_dir(FuVbeDevice *self)
{
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_VBE_DEVICE(self), NULL);
	return priv->vbe_dir;
}

static void
fu_vbe_device_init(FuVbeDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_protocol(FU_DEVICE(self), "org.vbe");
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED);
	fu_device_set_physical_id(FU_DEVICE(self), "vbe");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_icon(FU_DEVICE(self), "computer");
}

static gboolean
fu_vbe_device_probe(FuDevice *device, GError **error)
{
	FuVbeDevice *self = FU_VBE_DEVICE(device);
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *version = NULL;
	g_autofree gchar *version_bootloader = NULL;

	g_return_val_if_fail(FU_IS_VBE_DEVICE(device), FALSE);

	/* get a list of compatible strings */
	if (!fu_fdt_image_get_attr_strlist(priv->fdt_root,
					   FU_FIT_FIRMWARE_ATTR_COMPATIBLE,
					   &priv->compatible,
					   error))
		return FALSE;

	/* get baseclass shared attributes */
	fu_fdt_image_get_attr_str(priv->fdt_node, "cur-version", &version, NULL);
	if (version != NULL)
		fu_device_set_version(device, version);
	fu_fdt_image_get_attr_str(priv->fdt_node, "bootloader-version", &version_bootloader, NULL);
	if (version_bootloader != NULL)
		fu_device_set_version_bootloader(device, version_bootloader);

	/* success */
	return TRUE;
}

static void
fu_vbe_device_constructed(GObject *obj)
{
	FuVbeDevice *self = FU_VBE_DEVICE(obj);
	fu_device_add_instance_id(FU_DEVICE(self), "main-system-firmware");
}

static void
fu_vbe_device_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuVbeDevice *self = FU_VBE_DEVICE(obj);
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_FDT_ROOT:
		g_value_set_object(value, priv->fdt_root);
		break;
	case PROP_FDT_NODE:
		g_value_set_object(value, priv->fdt_node);
		break;
	case PROP_VBE_DIR:
		g_value_set_string(value, priv->vbe_dir);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fu_vbe_device_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuVbeDevice *self = FU_VBE_DEVICE(obj);
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_FDT_ROOT:
		g_set_object(&priv->fdt_root, g_value_get_object(value));
		break;
	case PROP_FDT_NODE:
		g_set_object(&priv->fdt_node, g_value_get_object(value));
		break;
	case PROP_VBE_DIR:
		g_free(priv->vbe_dir);
		priv->vbe_dir = g_strdup(g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fu_vbe_device_finalize(GObject *obj)
{
	FuVbeDevice *self = FU_VBE_DEVICE(obj);
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	g_free(priv->vbe_dir);
	g_strfreev(priv->compatible);
	if (priv->fdt_root != NULL)
		g_object_unref(priv->fdt_root);
	if (priv->fdt_node != NULL)
		g_object_unref(priv->fdt_node);
	G_OBJECT_CLASS(fu_vbe_device_parent_class)->finalize(obj);
}

static void
fu_vbe_device_class_init(FuVbeDeviceClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	object_class->get_property = fu_vbe_device_get_property;
	object_class->set_property = fu_vbe_device_set_property;

	pspec =
	    g_param_spec_object("fdt-root",
				NULL,
				"FDT root containing method parameters",
				FU_TYPE_FDT_IMAGE,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FDT_ROOT, pspec);

	pspec =
	    g_param_spec_object("fdt-node",
				NULL,
				"FDT image within the device tree containing method parameters'",
				FU_TYPE_FDT_IMAGE,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FDT_NODE, pspec);

	pspec =
	    g_param_spec_string("vbe-dir",
				NULL,
				"Directory containing state file for each VBE method",
				NULL,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_VBE_DIR, pspec);

	object_class->constructed = fu_vbe_device_constructed;
	object_class->finalize = fu_vbe_device_finalize;
	klass_device->to_string = fu_vbe_device_to_string;
	klass_device->probe = fu_vbe_device_probe;
}
