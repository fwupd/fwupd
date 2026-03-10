/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-lenovo-dock-image.h"
#include "fu-lenovo-dock-struct.h"

struct _FuLenovoDockImage {
	FuFirmware parent_instance;
	guint32 crc;
};

G_DEFINE_TYPE(FuLenovoDockImage, fu_lenovo_dock_image, FU_TYPE_FIRMWARE)

static void
fu_lenovo_dock_image_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuLenovoDockImage *self = FU_LENOVO_DOCK_IMAGE(firmware);
	fu_xmlb_builder_insert_kx(bn, "crc", self->crc);
}

guint32
fu_lenovo_dock_image_get_crc(FuLenovoDockImage *self)
{
	g_return_val_if_fail(FU_IS_LENOVO_DOCK_IMAGE(self), G_MAXUINT32);
	return self->crc;
}

void
fu_lenovo_dock_image_set_crc(FuLenovoDockImage *self, guint32 crc)
{
	g_return_if_fail(FU_IS_LENOVO_DOCK_IMAGE(self));
	self->crc = crc;
}

static gchar *
fu_lenovo_dock_image_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_lenovo_dock_image_init(FuLenovoDockImage *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_firmware_set_images_max(FU_FIRMWARE(self), G_MAXUINT8);
}

static void
fu_lenovo_dock_image_class_init(FuLenovoDockImageClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->export = fu_lenovo_dock_image_export;
	firmware_class->convert_version = fu_lenovo_dock_image_convert_version;
}

FuFirmware *
fu_lenovo_dock_image_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LENOVO_DOCK_IMAGE, NULL));
}
