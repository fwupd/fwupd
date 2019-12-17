/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-device.h"

typedef struct {
	FuUsbDevice		 parent_instance;
	FuVliDeviceKind		 kind;
} FuVliDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuVliDevice, fu_vli_device, FU_TYPE_USB_DEVICE)

#define GET_PRIVATE(o) (fu_vli_device_get_instance_private (o))

void
fu_vli_device_set_kind (FuVliDevice *self, FuVliDeviceKind device_kind)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	priv->kind = device_kind;
}

FuVliDeviceKind
fu_vli_device_get_kind (FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	return priv->kind;
}

static void
fu_vli_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (device);
	FuVliDevice *self = FU_VLI_DEVICE (device);
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	fu_common_string_append_kv (str, idt, "DeviceKind",
				    fu_vli_common_device_kind_to_string (priv->kind));

	/* subclassed further */
	if (klass->to_string != NULL)
		return klass->to_string (self, idt, str);
}

static gboolean
fu_vli_device_setup (FuDevice *device, GError **error)
{
	FuVliDevice *self = FU_VLI_DEVICE (device);
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (device);

	/* subclassed further */
	if (klass->setup != NULL)
		return klass->setup (self, error);

	/* success */
	return TRUE;
}

static void
fu_vli_device_init (FuVliDevice *self)
{
}

static void
fu_vli_device_class_init (FuVliDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_vli_device_to_string;
	klass_device->setup = fu_vli_device_setup;
}
