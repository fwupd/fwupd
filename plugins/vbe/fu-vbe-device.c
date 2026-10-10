/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vbe-device.h"

enum { PROP_0, PROP_FDT_ROOT, PROP_FDT_NODE, PROP_LAST };

typedef struct {
	FuFdtImage *fdt_root;
	FuFdtImage *fdt_node;
	gchar **compatible;
	gchar *storage; /* e.g. "mmc1" */
} FuVbeDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuVbeDevice, fu_vbe_device, FU_TYPE_UDEV_DEVICE)
#define GET_PRIVATE(o) (fu_vbe_device_get_instance_private(o))

static void
fu_vbe_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuVbeDevice *self = FU_VBE_DEVICE(device);
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->compatible != NULL) {
		g_autofree gchar *tmp = g_strjoinv(":", priv->compatible);
		fwupd_codec_string_append(str, idt, "Compatible", tmp);
	}
	fwupd_codec_string_append(str, idt, "Storage", priv->storage);
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

static gboolean
fu_vbe_device_parse_devnum(const gchar *str, guint *value, GError **error)
{
	guint64 val64 = 0;

	/* skip non-numeric part */
	while (*str != '\0' && !g_ascii_isdigit(*str))
		str++;

	/* convert to uint */
	if (!fu_strtoull(str, &val64, 0x0, G_MAXUINT, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	if (value != NULL)
		*value = val64;
	return TRUE;
}

static gboolean
fu_vbe_device_parse_storage(FuVbeDevice *self, GError **error)
{
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	guint devnum = 0;
	g_autofree gchar *devname = NULL;

	g_clear_pointer(&priv->storage, g_free);
	if (!fu_fdt_image_get_attr_str(priv->fdt_node, "storage", &priv->storage, error))
		return FALSE;

	/* if this is an absolute path, use it */
	if (g_str_has_prefix(priv->storage, "/")) {
		fu_udev_device_set_device_file(FU_UDEV_DEVICE(self), priv->storage);
		return TRUE;
	}

	/* obtain the 1 from "mmc1" */
	if (!fu_vbe_device_parse_devnum(priv->storage, &devnum, error)) {
		g_prefix_error(error, "cannot parse storage property %s: ", priv->storage);
		return FALSE;
	}
	if (!g_str_has_prefix(priv->storage, "mmc")) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported 'storage' media '%s'",
			    priv->storage);
		return FALSE;
	}
	devname = g_strdup_printf("/dev/mmcblk%u", devnum);
	fu_udev_device_set_device_file(FU_UDEV_DEVICE(self), devname);

	/* success */
	return TRUE;
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
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE);
	fu_device_set_physical_id(FU_DEVICE(self), "vbe");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	/* the devicetree properties are not saved, so the device cannot be emulated */
	fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static gboolean
fu_vbe_device_probe(FuDevice *device, GError **error)
{
	FuVbeDevice *self = FU_VBE_DEVICE(device);
	FuVbeDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *version = NULL;
	g_autofree gchar *version_bootloader = NULL;

	g_return_val_if_fail(FU_IS_VBE_DEVICE(device), FALSE);

	/*
	 * the device comes from the devicetree rather than from udev, so there is
	 * no sysfs node for FuUdevDevice to probe; this sets the device file itself
	 */

	/* sanity check */
	if (priv->fdt_root == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no FDT root");
		return FALSE;
	}

	/* each method has its own node, e.g. firmware0, so use it to tell them apart */
	if (priv->fdt_node != NULL)
		fu_device_set_logical_id(device, fu_firmware_get_id(FU_FIRMWARE(priv->fdt_node)));

	/* get a list of compatible strings, which not all firmware provides */
	g_clear_pointer(&priv->compatible, g_strfreev);
	if (!fu_fdt_image_get_attr_strlist(priv->fdt_root,
					   FU_FIT_FIRMWARE_ATTR_COMPATIBLE,
					   &priv->compatible,
					   NULL))
		g_debug("no compatible strings for the board");

	/* the board's compatible string selects the firmware for it */
	if (priv->compatible != NULL && priv->compatible[0] != NULL) {
		fu_device_add_instance_strsafe(device, "COMPATIBLE", priv->compatible[0]);
		if (!fu_device_build_instance_id(device, error, "VBE", "COMPATIBLE", NULL))
			return FALSE;
	}

	/* get baseclass shared attributes */
	fu_fdt_image_get_attr_str(priv->fdt_node, "cur-version", &version, NULL);
	if (version != NULL)
		fu_device_set_version(device, version);
	fu_fdt_image_get_attr_str(priv->fdt_node, "bootloader-version", &version_bootloader, NULL);
	if (version_bootloader != NULL)
		fu_device_set_version_bootloader(device, version_bootloader);

	/* all methods so far use a block device */
	if (!fu_vbe_device_parse_storage(self, error))
		return FALSE;

	/* success */
	return TRUE;
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
	g_strfreev(priv->compatible);
	g_free(priv->storage);
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
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	object_class->get_property = fu_vbe_device_get_property;
	object_class->set_property = fu_vbe_device_set_property;

	pspec =
	    g_param_spec_object("fdt-root",
				NULL,
				NULL,
				FU_TYPE_FDT_IMAGE,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FDT_ROOT, pspec);

	pspec =
	    g_param_spec_object("fdt-node",
				NULL,
				NULL,
				FU_TYPE_FDT_IMAGE,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FDT_NODE, pspec);

	object_class->finalize = fu_vbe_device_finalize;
	device_class->to_string = fu_vbe_device_to_string;
	device_class->probe = fu_vbe_device_probe;
}
