/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ifd-device.h"
#include "fu-intel-spi-device.h"

typedef struct {
	FuIfdRegion region;
	guint32 offset;
	FuIfdAccess access[FU_IFD_REGION_MAX];
} FuIfdDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuIfdDevice, fu_ifd_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_ifd_device_get_instance_private(o))

static void
fu_ifd_device_set_region(FuIfdDevice *self, FuIfdRegion region)
{
	FuIfdDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *region_str = fu_ifd_region_to_string(region);

	priv->region = region;
	fu_device_set_name(FU_DEVICE(self), fu_ifd_region_to_name(region));
	fu_device_set_logical_id(FU_DEVICE(self), region_str);

	/* add instance ID */
	fu_device_add_instance_strup(FU_DEVICE(self), "NAME", region_str);
	fu_device_build_instance_id(FU_DEVICE(self), NULL, "IFD", "NAME", NULL);
}

static void
fu_ifd_device_set_freg(FuIfdDevice *self, guint32 freg)
{
	FuIfdDevicePrivate *priv = GET_PRIVATE(self);
	guint32 freg_base = FU_IFD_FREG_BASE(freg);
	guint32 freg_limt = FU_IFD_FREG_LIMIT(freg);
	guint32 freg_size = (freg_limt - freg_base) + 1;

	priv->offset = freg_base;
	fu_device_set_firmware_size(FU_DEVICE(self), freg_size);
}

void
fu_ifd_device_set_access(FuIfdDevice *self, FuIfdRegion region, FuIfdAccess access)
{
	FuIfdDevicePrivate *priv = GET_PRIVATE(self);
	priv->access[region] = access;
}

static void
fu_ifd_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIfdDevice *self = FU_IFD_DEVICE(device);
	FuIfdDevicePrivate *priv = GET_PRIVATE(self);

	fu_string_append(str, idt, "Region", fu_ifd_region_to_string(priv->region));
	fu_string_append_kx(str, idt, "Offset", priv->offset);

	for (guint i = 0; i < FU_IFD_REGION_MAX; i++) {
		g_autofree gchar *title = NULL;
		if (priv->access[i] == FU_IFD_ACCESS_NONE)
			continue;
		title = g_strdup_printf("Access[%s]", fu_ifd_region_to_string(i));
		fu_string_append(str, idt, title, fu_ifd_access_to_string(priv->access[i]));
	}
}

static GBytes *
fu_ifd_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuIfdDevice *self = FU_IFD_DEVICE(device);
	FuIfdDevicePrivate *priv = GET_PRIVATE(self);
	FuDevice *parent = fu_device_get_parent(device);
	guint64 total_size = fu_device_get_firmware_size_max(device);
	return fu_intel_spi_device_dump(FU_INTEL_SPI_DEVICE(parent),
					device,
					priv->offset,
					total_size,
					progress,
					error);
}

static FuFirmware *
fu_ifd_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuIfdDevice *self = FU_IFD_DEVICE(device);
	FuIfdDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuFirmware) firmware = fu_ifd_image_new();
	g_autoptr(GBytes) blob = NULL;

	blob = fu_ifd_device_dump_firmware(device, progress, error);
	if (blob == NULL)
		return NULL;
	if (priv->region == FU_IFD_REGION_BIOS)
		firmware = fu_ifd_bios_new();
	else
		firmware = fu_ifd_image_new();
	if (!fu_firmware_parse(firmware, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static void
fu_ifd_device_init(FuIfdDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_icon(FU_DEVICE(self), "computer");
}

static void
fu_ifd_device_class_init(FuIfdDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_ifd_device_to_string;
	klass_device->dump_firmware = fu_ifd_device_dump_firmware;
	klass_device->read_firmware = fu_ifd_device_read_firmware;
}

FuDevice *
fu_ifd_device_new(FuContext *ctx, FuIfdRegion region, guint32 freg)
{
	FuIfdDevice *self = FU_IFD_DEVICE(g_object_new(FU_TYPE_IFD_DEVICE, "context", ctx, NULL));
	fu_ifd_device_set_region(self, region);
	fu_ifd_device_set_freg(self, freg);
	return FU_DEVICE(self);
}
