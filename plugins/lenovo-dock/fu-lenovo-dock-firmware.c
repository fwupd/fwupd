/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-lenovo-dock-firmware.h"
#include "fu-lenovo-dock-image.h"
#include "fu-lenovo-dock-struct.h"

struct _FuLenovoDockFirmware {
	FuFirmware parent_instance;
	guint16 pid;
};

G_DEFINE_TYPE(FuLenovoDockFirmware, fu_lenovo_dock_firmware, FU_TYPE_FIRMWARE)

static void
fu_lenovo_dock_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuLenovoDockFirmware *self = FU_LENOVO_DOCK_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "pid", self->pid);
}

static gboolean
fu_lenovo_dock_firmware_parse_image(FuLenovoDockFirmware *self,
				    GInputStream *stream_usage,
				    GInputStream *stream_composite,
				    gsize offset,
				    FuFirmwareParseFlags flags,
				    GError **error)
{
	guint32 physical_addr;
	guint32 target_crc32;
	guint32 target_size;
	guint32 max_size;
	FuLenovoDockComponentId component_id;
	g_autoptr(FuStructLenovoDockUsageItem) st_item = NULL;
	g_autoptr(GInputStream) stream_partial = NULL;
	g_autoptr(FuFirmware) img = fu_lenovo_dock_image_new();

	st_item = fu_struct_lenovo_dock_usage_item_parse_stream(stream_usage, offset, error);
	if (st_item == NULL)
		return FALSE;

	/* sanity check */
	component_id = fu_struct_lenovo_dock_usage_item_get_component_id(st_item);
	physical_addr = fu_struct_lenovo_dock_usage_item_get_address(st_item);
	max_size = fu_struct_lenovo_dock_usage_item_get_max_size(st_item);
	if (max_size > fu_firmware_get_size_max(img)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "max size 0x%x > 0x%x",
			    max_size,
			    (guint)fu_firmware_get_size_max(img));
		return FALSE;
	}
	target_size = fu_struct_lenovo_dock_usage_item_get_target_size(st_item);
	if (target_size == 0)
		return TRUE;
	if (target_size > max_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "target size 0x%x > max size 0x%x",
			    target_size,
			    max_size);
		return FALSE;
	}

	fu_firmware_set_idx(img, component_id);
	if (component_id <= FU_LENOVO_DOCK_COMPONENT_ID_DBG)
		fu_firmware_set_id(img, fu_lenovo_dock_component_id_to_string(component_id));
	fu_firmware_set_version_raw(img,
				    fu_struct_lenovo_dock_usage_item_get_target_version(st_item));
	fu_firmware_set_addr(img, physical_addr);
	if (!fu_firmware_add_image(FU_FIRMWARE(self), img, error))
		return FALSE;

	/* set image stream, and include the signature too */
	stream_partial =
	    fu_partial_input_stream_new(stream_composite,
					physical_addr,
					(gsize)target_size + FU_LENOVO_DOCK_FIRMWARE_SIGNATURE_SIZE,
					error);
	if (stream_partial == NULL)
		return FALSE;
	if (!fu_firmware_set_stream(img, stream_partial, error))
		return FALSE;

	/* check CRC */
	target_crc32 = fu_struct_lenovo_dock_usage_item_get_target_crc32(st_item);
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 crc_calculated = G_MAXUINT32;
		g_autoptr(GInputStream) stream_crc = NULL;

		stream_crc = fu_partial_input_stream_new(stream_composite,
							 (gsize)physical_addr +
							     FU_LENOVO_DOCK_FIRMWARE_SIGNATURE_SIZE,
							 target_size,
							 error);
		if (stream_crc == NULL)
			return FALSE;
		if (!fu_input_stream_compute_crc32(stream_crc,
						   FU_CRC_KIND_B32_STANDARD,
						   &crc_calculated,
						   error))
			return FALSE;
		if (target_crc32 != crc_calculated) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "usage item provided 0x%08x and calculated 0x%08x",
				    target_crc32,
				    crc_calculated);
			return FALSE;
		}
	}
	fu_lenovo_dock_image_set_crc(FU_LENOVO_DOCK_IMAGE(img), target_crc32);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_firmware_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      FuFirmwareParseFlags flags,
			      GError **error)
{
	FuLenovoDockFirmware *self = FU_LENOVO_DOCK_FIRMWARE(firmware);
	gsize offset = 0;
	guint8 usage_items;
	g_autoptr(FuFirmware) firmware_zip = fu_zip_firmware_new();
	g_autoptr(FuStructLenovoDockUsage) st = NULL;
	g_autoptr(GInputStream) stream_usage = NULL;
	g_autoptr(GInputStream) stream_composite = NULL;

	if (!fu_firmware_parse_stream(firmware_zip, stream, 0x0, flags, error))
		return FALSE;
	stream_usage =
	    fu_firmware_get_image_by_id_stream(firmware_zip, "*usage_information_table.bin", error);
	if (stream_usage == NULL)
		return FALSE;
	stream_composite =
	    fu_firmware_get_image_by_id_stream(firmware_zip, "*composite_image.bin", error);
	if (stream_composite == NULL)
		return FALSE;

	/* parse usage header and items */
	st = fu_struct_lenovo_dock_usage_parse_stream(stream_usage, offset, error);
	if (st == NULL)
		return FALSE;
	self->pid = fu_struct_lenovo_dock_usage_get_pid(st);
	fu_firmware_set_version_raw(firmware,
				    fu_struct_lenovo_dock_usage_get_composite_version(st));
	if (!fu_size_checked_inc(&offset, FU_STRUCT_LENOVO_DOCK_USAGE_SIZE, error))
		return FALSE;
	usage_items = fu_struct_lenovo_dock_usage_get_total_number(st);
	for (guint i = 0; i < usage_items; i++) {
		if (!fu_lenovo_dock_firmware_parse_image(self,
							 stream_usage,
							 stream_composite,
							 offset,
							 flags,
							 error))
			return FALSE;

		/* next! */
		if (!fu_size_checked_inc(&offset, FU_STRUCT_LENOVO_DOCK_USAGE_ITEM_SIZE, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

guint16
fu_lenovo_dock_firmware_get_pid(FuLenovoDockFirmware *self)
{
	g_return_val_if_fail(FU_IS_LENOVO_DOCK_FIRMWARE(self), G_MAXUINT16);
	return self->pid;
}

static gchar *
fu_lenovo_dock_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return g_strdup_printf("%X.%X.%02X",
			       (guint)(version_raw >> 16) & 0xFF,
			       (guint)(version_raw >> 8) & 0xFF,
			       (guint)version_raw & 0xFF);
}

static void
fu_lenovo_dock_firmware_init(FuLenovoDockFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_firmware_set_images_max(FU_FIRMWARE(self), G_MAXUINT8);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_LENOVO_DOCK_IMAGE);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 16 * FU_MB);
}

static void
fu_lenovo_dock_firmware_class_init(FuLenovoDockFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_lenovo_dock_firmware_parse;
	firmware_class->export = fu_lenovo_dock_firmware_export;
	firmware_class->convert_version = fu_lenovo_dock_firmware_convert_version;
}
