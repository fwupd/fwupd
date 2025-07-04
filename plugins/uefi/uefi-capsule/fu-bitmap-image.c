/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bitmap-image.h"
#include "fu-uefi-struct.h"

struct _FuBitmapImage {
	FuFirmware parent_instance;
	guint32 width;
	guint32 height;
};

G_DEFINE_TYPE(FuBitmapImage, fu_bitmap_image, FU_TYPE_FIRMWARE)

static void
fu_bitmap_image_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuBitmapImage *self = FU_BITMAP_IMAGE(firmware);
	fu_xmlb_builder_insert_kx(bn, "width", self->width);
	fu_xmlb_builder_insert_kx(bn, "height", self->height);
}

static gboolean
fu_bitmap_image_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      FuFirmwareParseFlags flags,
		      GError **error)
{
	FuBitmapImage *self = FU_BITMAP_IMAGE(firmware);
	g_autoptr(FuStructBitmapFileHeader) st_file = NULL;
	g_autoptr(FuStructBitmapInfoHeader) st_info = NULL;

	st_file = fu_struct_bitmap_file_header_parse_stream(stream, 0x0, error);
	if (st_file == NULL) {
		g_prefix_error(error, "image is corrupt: ");
		return FALSE;
	}
	fu_firmware_set_size(firmware, fu_struct_bitmap_file_header_get_size(st_file));
	st_info = fu_struct_bitmap_info_header_parse_stream(stream, st_file->len, error);
	if (st_info == NULL) {
		g_prefix_error(error, "header is corrupt: ");
		return FALSE;
	}
	self->width = fu_struct_bitmap_info_header_get_width(st_info);
	self->height = fu_struct_bitmap_info_header_get_height(st_info);

	/* success */
	return TRUE;
}

guint32
fu_bitmap_image_get_width(FuBitmapImage *self)
{
	g_return_val_if_fail(FU_IS_BITMAP_IMAGE(self), 0);
	return self->width;
}

guint32
fu_bitmap_image_get_height(FuBitmapImage *self)
{
	g_return_val_if_fail(FU_IS_BITMAP_IMAGE(self), 0);
	return self->height;
}

static void
fu_bitmap_image_class_init(FuBitmapImageClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_bitmap_image_parse;
	firmware_class->export = fu_bitmap_image_export;
}

static void
fu_bitmap_image_init(FuBitmapImage *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
}

FuBitmapImage *
fu_bitmap_image_new(void)
{
	FuBitmapImage *self;
	self = g_object_new(FU_TYPE_BITMAP_IMAGE, NULL);
	return FU_BITMAP_IMAGE(self);
}
