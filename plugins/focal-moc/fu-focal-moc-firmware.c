/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-firmware.h"
#include "fu-focal-moc-struct.h"

struct _FuFocalMocFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuFocalMocFirmware, fu_focal_moc_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_focal_moc_firmware_validate(FuFirmware *firmware,
			       FuInputStream *stream,
			       gsize offset,
			       GError **error)
{
	return fu_struct_focal_moc_firmware_header_validate_stream(stream, offset, error);
}

static gboolean
fu_focal_moc_firmware_parse(FuFirmware *firmware,
			    FuInputStream *stream,
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	gsize streamsz = 0;
	guint32 body_sz;
	guint32 entry_offset;
	guint32 version;
	g_autofree gchar *version_str = NULL;
	g_autoptr(FuStructFocalMocFirmwareHeader) st = NULL;

	st = fu_struct_focal_moc_firmware_header_parse_stream(stream, 0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_focal_moc_firmware_header_get_kind(st) != FU_FOCAL_MOC_FIRMWARE_KIND_APP) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware type invalid: got=0x%x expected=0x%x",
			    fu_struct_focal_moc_firmware_header_get_kind(st),
			    (guint)FU_FOCAL_MOC_FIRMWARE_KIND_APP);
		return FALSE;
	}
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	body_sz = fu_struct_focal_moc_firmware_header_get_size(st);
	if (body_sz == 0 || streamsz != FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE + body_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware size invalid: header=0x%x file=0x%x expected=0x%x",
			    body_sz,
			    (guint)streamsz,
			    FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE + body_sz);
		return FALSE;
	}
	entry_offset = fu_struct_focal_moc_firmware_header_get_entry_offset(st);
	if (entry_offset >= body_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware entry offset invalid: offset=0x%x size=0x%x",
			    entry_offset,
			    body_sz);
		return FALSE;
	}
	version = fu_struct_focal_moc_firmware_header_get_version(st);
	if (version > G_MAXUINT16) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware version exceeds four hex digits: 0x%x",
			    version);
		return FALSE;
	}
	version_str = g_strdup_printf("%04x", version);
	fu_firmware_set_version(firmware, version_str);
	if (!fu_firmware_set_stream(firmware, stream, error))
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_focal_moc_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = fu_firmware_get_bytes_with_patches(firmware, error);

	if (blob == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob);

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_focal_moc_firmware_init(FuFocalMocFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_PLAIN);
}

static void
fu_focal_moc_firmware_class_init(FuFocalMocFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_focal_moc_firmware_validate;
	firmware_class->parse = fu_focal_moc_firmware_parse;
	firmware_class->write = fu_focal_moc_firmware_write;
	fu_firmware_set_size_max(firmware_class,
				 (384 * FU_KB) - FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE);
}

FuFirmware *
fu_focal_moc_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_FOCAL_MOC_FIRMWARE, NULL));
}
