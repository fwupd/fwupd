/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-flashrom-device.h"
#include "fu-flashrom-context.h"

typedef struct {
	FuFlashromOpener *opener;
	FuFlashromContext *flashctx;
} FuFlashromDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuFlashromDevice, fu_flashrom_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_flashrom_device_get_instance_private (o))

void
fu_flashrom_device_set_programmer_name (FuFlashromDevice *self, const gchar *name)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FLASHROM_DEVICE (self));
	fu_flashrom_opener_set_programmer (priv->opener, name);
}

const gchar *
fu_flashrom_device_get_programmer_name (FuFlashromDevice *self)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_FLASHROM_DEVICE (self), NULL);
	return fu_flashrom_opener_get_programmer (priv->opener);
}

void
fu_flashrom_device_set_programmer_args (FuFlashromDevice *self, const gchar *args)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_FLASHROM_DEVICE (self));
	fu_flashrom_opener_set_programmer_args (priv->opener, args);
}

static void
fu_flashrom_device_init (FuFlashromDevice *self)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE (self);

	priv->opener = fu_flashrom_opener_new ();
	priv->flashctx = NULL;
	fu_device_add_protocol (FU_DEVICE (self), "org.flashrom");
}

static void
fu_flashrom_device_finalize (GObject *object)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE (FU_FLASHROM_DEVICE (object));
	if (priv->flashctx != NULL) {
		/* should have been closed in close() */
		g_warn_if_reached();
		g_object_unref (priv->flashctx);
	}
	g_object_unref (priv->opener);
	G_OBJECT_CLASS (fu_flashrom_device_parent_class)->finalize (object);
}

FuFlashromOpener *
fu_flashrom_device_get_opener (FuFlashromDevice *self)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE (self);

	return priv->opener;
}

FuFlashromContext *
fu_flashrom_device_get_context (FuFlashromDevice *self)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE (self);

	/* attempting to use context without device open */
	g_warn_if_fail(priv->flashctx != NULL);
	return priv->flashctx;
}

static gboolean
fu_flashrom_device_set_quirk_kv (FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	if (g_strcmp0 (key, "PciBcrAddr") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		fu_device_set_metadata_integer (device, "PciBcrAddr", tmp);
		return TRUE;
	}
	if (g_strcmp0 (key, "FlashromProgrammer") == 0) {
		fu_flashrom_device_set_programmer_name (FU_FLASHROM_DEVICE (device), value);
		return TRUE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "not supported");
	return FALSE;
}

static gboolean
fu_flashrom_device_probe (FuDevice *device, GError **error)
{
	const gchar *dev_name = NULL;
	const gchar *sysfs_path = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS (fu_flashrom_device_parent_class)->probe (device, error))
		return FALSE;

	sysfs_path = fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (device));
	if (sysfs_path != NULL) {
		g_autofree gchar *physical_id = NULL;
		physical_id = g_strdup_printf ("DEVNAME=%s", sysfs_path);
		fu_device_set_physical_id (device, physical_id);
	}
	dev_name = fu_udev_device_get_sysfs_attr (FU_UDEV_DEVICE (device), "name", NULL);
	if (dev_name != NULL) {
		fu_device_add_instance_id_full (device, dev_name,
						FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}
	return TRUE;
}

static gboolean
fu_flashrom_device_open (FuDevice *device, GError **error)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE (FU_FLASHROM_DEVICE (device));

	if (priv->flashctx != NULL)
		return TRUE;
	return fu_flashrom_context_open (priv->opener, &priv->flashctx, error);
}

static gboolean
fu_flashrom_device_close (FuDevice *device, GError **error)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE (FU_FLASHROM_DEVICE (device));

	g_clear_object (&priv->flashctx);
	return TRUE;
}

static void
fu_flashrom_device_class_init (FuFlashromDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_flashrom_device_finalize;
	klass_device->set_quirk_kv = fu_flashrom_device_set_quirk_kv;
	klass_device->probe = fu_flashrom_device_probe;
	klass_device->open = fu_flashrom_device_open;
	klass_device->close = fu_flashrom_device_close;
}
