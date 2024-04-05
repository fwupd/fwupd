/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-phat-struct.h"
#include "fu-acpi-phat-version-element.h"
#include "fu-acpi-phat-version-record.h"
#include "fu-acpi-phat.h"

struct _FuAcpiPhatVersionRecord {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuAcpiPhatVersionRecord, fu_acpi_phat_version_record, FU_TYPE_FIRMWARE)

static gboolean
fu_acpi_phat_version_record_parse(FuFirmware *firmware,
				  GInputStream *stream,
				  gsize offset,
				  FwupdInstallFlags flags,
				  GError **error)
{
	guint32 record_count = 0;
	g_autoptr(GByteArray) st = NULL;

	st = fu_struct_acpi_phat_version_record_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;
	record_count = fu_struct_acpi_phat_version_record_get_record_count(st);
	for (guint32 i = 0; i < record_count; i++) {
		g_autoptr(FuFirmware) firmware_tmp = fu_acpi_phat_version_element_new();
		g_autoptr(GInputStream) stream_tmp = NULL;
		stream_tmp = fu_partial_input_stream_new(stream,
							 offset + st->len,
							 FU_STRUCT_ACPI_PHAT_VERSION_ELEMENT_SIZE);
		fu_firmware_set_offset(firmware_tmp, offset + st->len);
		if (!fu_firmware_parse_stream(firmware_tmp,
					      stream_tmp,
					      0x0,
					      flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
					      error))
			return FALSE;
		if (!fu_firmware_add_image_full(firmware, firmware_tmp, error))
			return FALSE;
		offset += fu_firmware_get_size(firmware_tmp);
	}
	return TRUE;
}

static GByteArray *
fu_acpi_phat_version_record_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf2 = g_byte_array_new();
	g_autoptr(GByteArray) st = fu_struct_acpi_phat_version_record_new();
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* write each element so we get the image size */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf2, blob);
	}

	/* data record */
	fu_struct_acpi_phat_version_record_set_rcdlen(st, st->len + buf2->len);
	fu_struct_acpi_phat_version_record_set_version(st, fu_firmware_get_version_raw(firmware));
	fu_struct_acpi_phat_version_record_set_record_count(st, images->len);

	/* element data */
	g_byte_array_append(st, buf2->data, buf2->len);
	return g_steal_pointer(&st);
}

static void
fu_acpi_phat_version_record_init(FuAcpiPhatVersionRecord *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 2000);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	g_type_ensure(FU_TYPE_ACPI_PHAT_VERSION_ELEMENT);
}

static void
fu_acpi_phat_version_record_class_init(FuAcpiPhatVersionRecordClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_acpi_phat_version_record_parse;
	firmware_class->write = fu_acpi_phat_version_record_write;
}

FuFirmware *
fu_acpi_phat_version_record_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_PHAT_VERSION_RECORD, NULL));
}
