/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-backend.h"
#include "fu-uefi-nvram-device.h"

typedef struct {
	GType device_gtype;
} FuUefiBackendPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUefiBackend, fu_uefi_backend, FU_TYPE_BACKEND)

#define GET_PRIVATE(o) (fu_uefi_backend_get_instance_private(o))

void
fu_uefi_backend_set_device_gtype(FuUefiBackend *self, GType device_gtype)
{
	FuUefiBackendPrivate *priv = GET_PRIVATE(self);
	priv->device_gtype = device_gtype;
}

GType
fu_uefi_backend_get_device_gtype(FuUefiBackend *self)
{
	FuUefiBackendPrivate *priv = GET_PRIVATE(self);
	return priv->device_gtype;
}

static void
fu_uefi_backend_to_string(FuBackend *backend, guint idt, GString *str)
{
	FuUefiBackend *self = FU_UEFI_BACKEND(backend);
	FuUefiBackendPrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "DeviceGType", g_type_name(priv->device_gtype));
}

/* create virtual object not backed by an ESRT entry */
FuUefiDevice *
fu_uefi_backend_device_new_from_dev(FuUefiBackend *self, FuDevice *dev)
{
	FuUefiDevice *device;
	FuUefiBackendPrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	g_return_val_if_fail(fu_device_get_guid_default(dev) != NULL, NULL);

	tmp = fu_device_get_metadata(dev, FU_DEVICE_METADATA_UEFI_DEVICE_KIND);
	device =
	    g_object_new(priv->device_gtype,
			 "fw-class",
			 fu_device_get_guid_default(dev),
			 "kind",
			 fu_uefi_device_kind_from_string(tmp),
			 "capsule-flags",
			 fu_device_get_metadata_integer(dev, FU_DEVICE_METADATA_UEFI_CAPSULE_FLAGS),
			 "fw-version",
			 fu_device_get_metadata_integer(dev, FU_DEVICE_METADATA_UEFI_FW_VERSION),
			 NULL);
	fu_device_incorporate(FU_DEVICE(device), dev, FU_DEVICE_INCORPORATE_FLAG_ALL);
	return device;
}

static void
fu_uefi_backend_init(FuUefiBackend *self)
{
	FuUefiBackendPrivate *priv = GET_PRIVATE(self);
	priv->device_gtype = FU_TYPE_UEFI_NVRAM_DEVICE;
}

static void
fu_uefi_backend_class_init(FuUefiBackendClass *klass)
{
	FuBackendClass *backend_class = FU_BACKEND_CLASS(klass);
	backend_class->to_string = fu_uefi_backend_to_string;
}
