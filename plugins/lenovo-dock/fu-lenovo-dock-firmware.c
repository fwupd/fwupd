/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-lenovo-dock-firmware.h"
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
fu_lenovo_dock_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuLenovoDockFirmware *self = FU_LENOVO_DOCK_FIRMWARE(firmware);
	guint64 tmp;

	/* load from .builder.xml */
	tmp = xb_node_query_text_as_uint(n, "pid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->pid = tmp;

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
	offset += FU_STRUCT_LENOVO_DOCK_USAGE_SIZE;
	usage_items = fu_struct_lenovo_dock_usage_get_total_number(st);
	for (guint i = 0; i < usage_items; i++) {
		guint32 physical_addr;
		guint32 target_size;
		guint32 max_size;
		FuLenovoDockFlashId flash_id;
		g_autoptr(FuStructLenovoDockUsageItem) st_item = NULL;
		g_autoptr(GInputStream) stream_partial = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();

		st_item =
		    fu_struct_lenovo_dock_usage_item_parse_stream(stream_usage, offset, error);
		if (st_item == NULL)
			return FALSE;

		/* sanity check */
		flash_id = fu_struct_lenovo_dock_usage_item_get_flash_id(st_item);
		physical_addr = fu_struct_lenovo_dock_usage_item_get_address(st_item);
		max_size = fu_struct_lenovo_dock_usage_item_get_max_size(st_item);
		target_size = fu_struct_lenovo_dock_usage_item_get_target_size(st_item);
		if (target_size == 0)
			continue;
		if (target_size > max_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "target size 0x%x > max size 0x%x",
				    target_size,
				    max_size);
			return FALSE;
		}

		fu_firmware_set_idx(img, flash_id);
		if (flash_id <= FU_LENOVO_DOCK_FLASH_ID_DBG)
			fu_firmware_set_id(img, fu_lenovo_dock_flash_id_to_string(flash_id));
		fu_firmware_set_version_raw(
		    img,
		    fu_struct_lenovo_dock_usage_item_get_target_version(st_item));
		fu_firmware_set_addr(img, physical_addr);
		if (!fu_firmware_add_image(firmware, img, error))
			return FALSE;

		/* set image stream */
		stream_partial =
		    fu_partial_input_stream_new(stream_composite,
						physical_addr,
						target_size + FU_LENOVO_DOCK_DEVICE_SIGNATURE_SIZE,
						error);
		if (stream_partial == NULL)
			return FALSE;
		if (!fu_firmware_set_stream(img, stream_partial, error))
			return FALSE;

		/* check CRC */
		if (target_size > 0) {
			// guint32 crc_calculated = 0xFFFFFFFF;
			guint32 crc_calculated = 0;
			guint32 crc_provided =
			    fu_struct_lenovo_dock_usage_item_get_target_crc32(st_item);
			g_autoptr(GInputStream) stream_crc = NULL;

			fu_firmware_add_flag(img, FU_FIRMWARE_FLAG_HAS_CHECKSUM);
			fu_firmware_add_flag(img, FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
			stream_crc = fu_partial_input_stream_new(stream_composite,
								 physical_addr,
								 max_size,
								 error);
			if (stream_crc == NULL)
				return FALSE;
			if (!fu_input_stream_compute_crc32(stream_crc,
							   FU_CRC_KIND_B32_STANDARD,
							   &crc_calculated,
							   error))
				return FALSE;
			if (crc_provided != crc_calculated) {
				g_warning("usage item provided 0x%04x and calculated 0x%04x",
					  crc_provided,
					  crc_calculated);
			}
			if (0 && crc_provided != crc_calculated) { /* FIXME */
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "usage item provided 0x%04x and calculated 0x%04x",
					    crc_provided,
					    crc_calculated);
				return FALSE;
			}
		}
		offset += FU_STRUCT_LENOVO_DOCK_USAGE_ITEM_SIZE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_lenovo_dock_firmware_write(FuFirmware *firmware, GError **error)
{
	FuLenovoDockFirmware *self = FU_LENOVO_DOCK_FIRMWARE(firmware);
	g_autoptr(FuStructLenovoDockUsage) st = fu_struct_lenovo_dock_usage_new();

	/* header first */
	fu_struct_lenovo_dock_usage_set_pid(st, self->pid);

	/* success */
	return g_byte_array_ref(st->buf);
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
	return fu_version_from_uint24(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_lenovo_dock_firmware_init(FuLenovoDockFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_firmware_set_images_max(FU_FIRMWARE(self), G_MAXUINT8);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
}

static void
fu_lenovo_dock_firmware_finalize(GObject *object)
{
	//	FuLenovoDockFirmware *self = FU_LENOVO_DOCK_FIRMWARE(object);
	G_OBJECT_CLASS(fu_lenovo_dock_firmware_parent_class)->finalize(object);
}

static void
fu_lenovo_dock_firmware_class_init(FuLenovoDockFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_lenovo_dock_firmware_finalize;
	firmware_class->parse = fu_lenovo_dock_firmware_parse;
	firmware_class->write = fu_lenovo_dock_firmware_write;
	firmware_class->build = fu_lenovo_dock_firmware_build;
	firmware_class->export = fu_lenovo_dock_firmware_export;
	firmware_class->convert_version = fu_lenovo_dock_firmware_convert_version;
}

FuFirmware *
fu_lenovo_dock_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LENOVO_DOCK_FIRMWARE, NULL));
}
