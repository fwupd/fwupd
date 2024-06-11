/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bitmap-image.h"
#include "fu-uefi-bgrt.h"
#include "fu-uefi-common.h"

struct _FuUefiBgrt {
	GObject parent_instance;
	guint32 xoffset;
	guint32 yoffset;
	guint32 width;
	guint32 height;
};

G_DEFINE_TYPE(FuUefiBgrt, fu_uefi_bgrt, G_TYPE_OBJECT)

gboolean
fu_uefi_bgrt_setup(FuUefiBgrt *self, GError **error)
{
	guint64 type;
	guint64 version;
	g_autofree gchar *bgrtdir = NULL;
	g_autofree gchar *imagefn = NULL;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autoptr(FuBitmapImage) bmp_image = fu_bitmap_image_new();
	g_autoptr(GFile) file = NULL;

	g_return_val_if_fail(FU_IS_UEFI_BGRT(self), FALSE);

	sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	bgrtdir = g_build_filename(sysfsfwdir, "acpi", "bgrt", NULL);
	if (!g_file_test(bgrtdir, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "BGRT is not supported");
		return FALSE;
	}
	type = fu_uefi_read_file_as_uint64(bgrtdir, "type");
	if (type != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "BGRT type was %" G_GUINT64_FORMAT,
			    type);
		return FALSE;
	}
	version = fu_uefi_read_file_as_uint64(bgrtdir, "version");
	if (version != 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "BGRT version was %" G_GUINT64_FORMAT,
			    version);
		return FALSE;
	}

	/* load image */
	self->xoffset = fu_uefi_read_file_as_uint64(bgrtdir, "xoffset");
	self->yoffset = fu_uefi_read_file_as_uint64(bgrtdir, "yoffset");
	imagefn = g_build_filename(bgrtdir, "image", NULL);
	file = g_file_new_build_filename(bgrtdir, "image", NULL);
	if (!fu_firmware_parse_file(FU_FIRMWARE(bmp_image), file, FWUPD_INSTALL_FLAG_NONE, error)) {
		g_prefix_error(error, "BGRT image invalid: ");
		return FALSE;
	}
	self->width = fu_bitmap_image_get_width(bmp_image);
	self->height = fu_bitmap_image_get_height(bmp_image);

	/* success */
	return TRUE;
}

gboolean
fu_uefi_bgrt_get_supported(FuUefiBgrt *self)
{
	g_return_val_if_fail(FU_IS_UEFI_BGRT(self), FALSE);
	if (self->width == 0 || self->height == 0)
		return FALSE;
	return TRUE;
}

guint32
fu_uefi_bgrt_get_xoffset(FuUefiBgrt *self)
{
	g_return_val_if_fail(FU_IS_UEFI_BGRT(self), 0);
	return self->xoffset;
}

guint32
fu_uefi_bgrt_get_yoffset(FuUefiBgrt *self)
{
	g_return_val_if_fail(FU_IS_UEFI_BGRT(self), 0);
	return self->yoffset;
}

guint32
fu_uefi_bgrt_get_width(FuUefiBgrt *self)
{
	g_return_val_if_fail(FU_IS_UEFI_BGRT(self), 0);
	return self->width;
}

guint32
fu_uefi_bgrt_get_height(FuUefiBgrt *self)
{
	g_return_val_if_fail(FU_IS_UEFI_BGRT(self), 0);
	return self->height;
}

static void
fu_uefi_bgrt_class_init(FuUefiBgrtClass *klass)
{
}

static void
fu_uefi_bgrt_init(FuUefiBgrt *self)
{
}

FuUefiBgrt *
fu_uefi_bgrt_new(void)
{
	FuUefiBgrt *self;
	self = g_object_new(FU_TYPE_UEFI_BGRT, NULL);
	return FU_UEFI_BGRT(self);
}
