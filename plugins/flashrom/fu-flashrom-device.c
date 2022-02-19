/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <libflashrom.h>

#include "fu-flashrom-device.h"

typedef struct {
	gchar *programmer_name;
	gchar *programmer_args;
	gsize flash_size;
	struct flashrom_flashctx *flashctx;
	struct flashrom_programmer *flashprog;
} FuFlashromDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuFlashromDevice, fu_flashrom_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_flashrom_device_get_instance_private(o))

void
fu_flashrom_device_set_programmer_name(FuFlashromDevice *self, const gchar *name)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FLASHROM_DEVICE(self));
	if (g_strcmp0(priv->programmer_name, name) == 0)
		return;
	g_free(priv->programmer_name);
	priv->programmer_name = g_strdup(name);
}

const gchar *
fu_flashrom_device_get_programmer_name(FuFlashromDevice *self)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FLASHROM_DEVICE(self), NULL);
	return priv->programmer_name;
}

void
fu_flashrom_device_set_programmer_args(FuFlashromDevice *self, const gchar *args)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FLASHROM_DEVICE(self));
	if (g_strcmp0(priv->programmer_args, args) == 0)
		return;
	g_free(priv->programmer_args);
	priv->programmer_args = g_strdup(args);
}

gsize
fu_flashrom_device_get_flash_size(FuFlashromDevice *self)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FLASHROM_DEVICE(self), 0);
	return priv->flash_size;
}

struct flashrom_flashctx *
fu_flashrom_device_get_flashctx(FuFlashromDevice *self)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FLASHROM_DEVICE(self), NULL);
	return priv->flashctx;
}

static void
fu_flashrom_device_init(FuFlashromDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "org.flashrom");
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED);
}

static void
fu_flashrom_device_finalize(GObject *object)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE(FU_FLASHROM_DEVICE(object));
	g_free(priv->programmer_name);
	g_free(priv->programmer_args);
	G_OBJECT_CLASS(fu_flashrom_device_parent_class)->finalize(object);
}

static gboolean
fu_flashrom_device_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	if (g_strcmp0(key, "PciBcrAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		fu_device_set_metadata_integer(device, "PciBcrAddr", tmp);
		return TRUE;
	}
	if (g_strcmp0(key, "FlashromProgrammer") == 0) {
		fu_flashrom_device_set_programmer_name(FU_FLASHROM_DEVICE(device), value);
		return TRUE;
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static gboolean
fu_flashrom_device_probe(FuDevice *device, GError **error)
{
	const gchar *dev_name = NULL;
	const gchar *sysfs_path = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_flashrom_device_parent_class)->probe(device, error))
		return FALSE;

	sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	if (sysfs_path != NULL) {
		g_autofree gchar *physical_id = NULL;
		physical_id = g_strdup_printf("DEVNAME=%s", sysfs_path);
		fu_device_set_physical_id(device, physical_id);
	}
	dev_name = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "name", NULL);
	if (dev_name != NULL) {
		fu_device_add_instance_id_full(device,
					       dev_name,
					       FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}
	return TRUE;
}

static gboolean
fu_flashrom_device_open(FuDevice *device, GError **error)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE(FU_FLASHROM_DEVICE(device));
	gint rc;

	if (priv->programmer_name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "programmer not specified");
		return FALSE;
	}

	if (flashrom_programmer_init(&priv->flashprog,
				     priv->programmer_name,
				     priv->programmer_args)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "programmer initialization failed");
		return FALSE;
	}
	rc = flashrom_flash_probe(&priv->flashctx, priv->flashprog, NULL);
	if (rc == 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash probe failed: multiple chips were found");
		return FALSE;
	}
	if (rc == 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash probe failed: no chip was found");
		return FALSE;
	}
	if (rc != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash probe failed: unknown error");
		return FALSE;
	}
	priv->flash_size = flashrom_flash_getsize(priv->flashctx);
	if (priv->flash_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash size zero");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_flashrom_device_close(FuDevice *device, GError **error)
{
	FuFlashromDevicePrivate *priv = GET_PRIVATE(FU_FLASHROM_DEVICE(device));
	flashrom_flash_release(priv->flashctx);
	flashrom_programmer_shutdown(priv->flashprog);
	return TRUE;
}

static void
fu_flashrom_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100); /* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0);	/* reload */
}

static void
fu_flashrom_device_class_init(FuFlashromDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_flashrom_device_finalize;
	klass_device->set_quirk_kv = fu_flashrom_device_set_quirk_kv;
	klass_device->probe = fu_flashrom_device_probe;
	klass_device->open = fu_flashrom_device_open;
	klass_device->close = fu_flashrom_device_close;
	klass_device->set_progress = fu_flashrom_device_set_progress;
}
