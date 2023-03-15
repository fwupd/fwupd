/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-acpi-phat-version-element.h"
#include "fu-acpi-phat-version-record.h"
#include "fu-acpi-phat.h"

struct _FuAcpiPhatVersionRecord {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuAcpiPhatVersionRecord, fu_acpi_phat_version_record, FU_TYPE_FIRMWARE)

static gboolean
fu_acpi_phat_version_record_parse(FuFirmware *firmware,
				  GBytes *fw,
				  gsize offset,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuStruct *st = fu_struct_lookup(firmware, "PhatVersionRecordHdr");
	gsize bufsz = 0;
	guint32 record_count = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (!fu_struct_unpack_full(st, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
		return FALSE;
	record_count = fu_struct_get_u32(st, "record_count");
	for (guint32 i = 0; i < record_count; i++) {
		g_autoptr(FuFirmware) firmware_tmp = fu_acpi_phat_version_element_new();
		g_autoptr(GBytes) fw_tmp = NULL;
		FuStruct *st_ele = fu_struct_lookup(firmware_tmp, "PhatVersionElement");
		fw_tmp = fu_bytes_new_offset(fw,
					     offset + fu_struct_size(st),
					     fu_struct_size(st_ele),
					     error);
		if (fw_tmp == NULL)
			return FALSE;
		fu_firmware_set_offset(firmware_tmp, offset + fu_struct_size(st));
		if (!fu_firmware_parse(firmware_tmp,
				       fw_tmp,
				       flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
				       error))
			return FALSE;
		fu_firmware_add_image(firmware, firmware_tmp);
		offset += fu_firmware_get_size(firmware_tmp);
	}
	return TRUE;
}

static GBytes *
fu_acpi_phat_version_record_write(FuFirmware *firmware, GError **error)
{
	FuStruct *st = fu_struct_lookup(firmware, "PhatVersionRecordHdr");
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) buf2 = g_byte_array_new();
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
	fu_struct_set_u16(st, "rcdlen", fu_struct_size(st) + buf2->len);
	fu_struct_set_u8(st, "version", fu_firmware_get_version_raw(firmware));
	fu_struct_set_u32(st, "record_count", images->len);
	buf = fu_struct_pack(st);

	/* element data */
	g_byte_array_append(buf, buf2->data, buf2->len);
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_acpi_phat_version_record_init(FuAcpiPhatVersionRecord *self)
{
	fu_struct_register(self,
			   "PhatVersionRecordHdr {"
			   "    signature: u16le: 0x0,"
			   "    rcdlen: u16le,"
			   "    version: u8,"
			   "    reserved: 3u8,"
			   "    record_count: u32le,"
			   "}");
}

static void
fu_acpi_phat_version_record_class_init(FuAcpiPhatVersionRecordClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_acpi_phat_version_record_parse;
	klass_firmware->write = fu_acpi_phat_version_record_write;
}

FuFirmware *
fu_acpi_phat_version_record_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_PHAT_VERSION_RECORD, NULL));
}
