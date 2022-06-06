/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-tpm-device.h"

typedef struct {
	guint idx;
	gchar *checksum;
} FuTpmDevicePcrItem;

typedef struct {
	gchar *family;
	GPtrArray *items; /* of FuTpmDevicePcrItem */
} FuTpmDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuTpmDevice, fu_tpm_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_tpm_device_get_instance_private(o))

void
fu_tpm_device_set_family(FuTpmDevice *self, const gchar *family)
{
	FuTpmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_TPM_DEVICE(self));
	priv->family = g_strdup(family);
}

const gchar *
fu_tpm_device_get_family(FuTpmDevice *self)
{
	FuTpmDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_TPM_DEVICE(self), NULL);
	return priv->family;
}

void
fu_tpm_device_add_checksum(FuTpmDevice *self, guint idx, const gchar *checksum)
{
	FuTpmDevicePrivate *priv = GET_PRIVATE(self);
	FuTpmDevicePcrItem *item = g_new0(FuTpmDevicePcrItem, 1);

	g_return_if_fail(FU_IS_TPM_DEVICE(self));
	g_return_if_fail(checksum != NULL);

	item->idx = idx;
	item->checksum = g_strdup(checksum);
	g_debug("added PCR-%02u=%s", item->idx, item->checksum);
	g_ptr_array_add(priv->items, item);
}

GPtrArray *
fu_tpm_device_get_checksums(FuTpmDevice *self, guint idx)
{
	FuTpmDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(FU_IS_TPM_DEVICE(self), NULL);

	for (guint i = 0; i < priv->items->len; i++) {
		FuTpmDevicePcrItem *item = g_ptr_array_index(priv->items, i);
		if (item->idx == idx)
			g_ptr_array_add(array, g_strdup(item->checksum));
	}
	return g_steal_pointer(&array);
}

static void
fu_tpm_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuTpmDevice *self = FU_TPM_DEVICE(device);
	FuTpmDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->family != NULL)
		fu_string_append(str, idt, "Family", priv->family);
}

static void
fu_tpm_v2_device_item_free(FuTpmDevicePcrItem *item)
{
	g_free(item->checksum);
	g_free(item);
}

static void
fu_tpm_device_finalize(GObject *object)
{
	FuTpmDevice *self = FU_TPM_DEVICE(object);
	FuTpmDevicePrivate *priv = GET_PRIVATE(self);

	g_free(priv->family);
	g_ptr_array_unref(priv->items);

	G_OBJECT_CLASS(fu_tpm_device_parent_class)->finalize(object);
}

static void
fu_tpm_device_init(FuTpmDevice *self)
{
	FuTpmDevicePrivate *priv = GET_PRIVATE(self);
	priv->items = g_ptr_array_new_with_free_func((GDestroyNotify)fu_tpm_v2_device_item_free);
	fu_device_set_name(FU_DEVICE(self), "TPM");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_NONE);
	fu_device_add_instance_id_full(FU_DEVICE(self),
				       "system-tpm",
				       FU_DEVICE_INSTANCE_FLAG_NO_QUIRKS);
}

static void
fu_tpm_device_class_init(FuTpmDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_tpm_device_finalize;
	klass_device->to_string = fu_tpm_device_to_string;
}
