/*
 * Copyright 2025 Joe hong <JoeHung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-capsule-firmware.h"
#include "fu-ilitek-its-common.h"
#include "fu-ilitek-its-struct.h"

typedef struct {
	guint16 fwid;
	guint32 type;
	guint8 sensor_id;
	guint8 sensor_id_mask;
	guint32 edid;
	guint32 edid_mask;
} FuIlitekItsLookupItem;

struct _FuIlitekItsCapsuleFirmware {
	FuFirmware parent_instance;

	guint32 package_ver;
	guint8 lookup_cnt;
	guint8 sku_cnt;

	FuIlitekItsLookupItem lookups[256];
};

G_DEFINE_TYPE(FuIlitekItsCapsuleFirmware, fu_ilitek_its_capsule_firmware, FU_TYPE_FIRMWARE)

static void
fu_ilitek_its_capsule_firmware_export(FuFirmware *firmware,
				      FuFirmwareExportFlags flags,
				      XbBuilderNode *bn)
{
	FuIlitekItsCapsuleFirmware *self = FU_ILITEK_ITS_CAPSULE_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "package_ver", self->package_ver);
	fu_xmlb_builder_insert_kx(bn, "lookup_cnt", self->lookup_cnt);
	fu_xmlb_builder_insert_kx(bn, "sku_cnt", self->sku_cnt);
}

static gboolean
fu_ilitek_its_capsule_firmware_validate(FuFirmware *firmware,
					GInputStream *stream,
					gsize offset,
					GError **error)
{
	gsize streamsz = 0;
	guint16 crc;
	g_autoptr(FuStructIlitekItsLookupHeader) auth_header = NULL;
	g_autoptr(GByteArray) capsule;
	g_autoptr(GBytes) bytes = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (!fu_struct_ilitek_its_capsule_header_validate_stream(stream, offset, error))
		return FALSE;
	capsule = fu_struct_ilitek_its_capsule_parse_stream(stream, 0, error); // FIXME offset?
	if (capsule == NULL)
		return FALSE;

	auth_header = fu_struct_ilitek_its_capsule_get_auth(capsule);

	// FIXME: this should use fu_input_stream_compute_crc16() instead
	bytes = fu_input_stream_read_bytes(stream,
					   FU_STRUCT_ILITEK_ITS_CAPSULE_OFFSET_LOOKUP,
					   streamsz - FU_STRUCT_ILITEK_ITS_CAPSULE_OFFSET_LOOKUP,
					   NULL,
					   error);
	if (bytes == NULL)
		return FALSE;

	crc = fu_ilitek_its_get_crc(bytes, g_bytes_get_size(bytes));
	if (crc != fu_struct_ilitek_its_auth_header_get_crc(auth_header)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "actual/header crc: 0x%x/0x%x not matched",
			    crc,
			    fu_struct_ilitek_its_auth_header_get_crc(auth_header));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_ilitek_its_capsule_firmware_parse(FuFirmware *firmware,
				     GInputStream *stream,
				     FuFirmwareParseFlags flags,
				     GError **error)
{
	FuIlitekItsCapsuleFirmware *self = FU_ILITEK_ITS_CAPSULE_FIRMWARE(firmware);
	gsize offset;
	g_autoptr(FuStructIlitekItsCapsule) capsule = NULL;
	g_autoptr(FuStructIlitekItsCapsule) capsule_header = NULL;
	g_autoptr(FuStructIlitekItsLookupHeader) lookup_header = NULL;
	g_autoptr(FuStructIlitekItsSkuHeader) sku_header = NULL;

	capsule = fu_struct_ilitek_its_capsule_parse_stream(stream, 0, error);
	if (capsule == NULL)
		return FALSE;

	capsule_header = fu_struct_ilitek_its_capsule_get_header(capsule);
	self->package_ver = fu_struct_ilitek_its_capsule_header_get_package_ver(capsule_header);

	lookup_header = fu_struct_ilitek_its_capsule_get_lookup(capsule);
	self->lookup_cnt = fu_struct_ilitek_its_lookup_header_get_cnt(lookup_header);

	offset =
	    FU_STRUCT_ILITEK_ITS_CAPSULE_OFFSET_LOOKUP + FU_STRUCT_ILITEK_ITS_LOOKUP_HEADER_SIZE;
	for (guint i = 0; i < self->lookup_cnt; i++) {
		g_autoptr(FuStructIlitekItsLookupHeader) lookup =
		    fu_struct_ilitek_its_lookup_item_parse_stream(stream, offset, error);

		self->lookups[i].type = fu_struct_ilitek_its_lookup_item_get_type(lookup);
		self->lookups[i].edid = fu_struct_ilitek_its_lookup_item_get_edid(lookup);
		self->lookups[i].sensor_id = fu_struct_ilitek_its_lookup_item_get_sensor_id(lookup);
		self->lookups[i].sensor_id_mask =
		    fu_struct_ilitek_its_lookup_item_get_sensor_id_mask(lookup);
		self->lookups[i].fwid = fu_struct_ilitek_its_lookup_item_get_fwid(lookup);

		offset += FU_STRUCT_ILITEK_ITS_LOOKUP_ITEM_SIZE;
	}

	sku_header = fu_struct_ilitek_its_sku_header_parse_stream(stream, offset, error);
	if (sku_header == NULL)
		return FALSE;

	self->sku_cnt = fu_struct_ilitek_its_sku_header_get_cnt(sku_header);

	offset += fu_struct_ilitek_its_sku_header_get_size(sku_header);
	for (guint i = 0; i < self->sku_cnt; i++) {
		g_autoptr(FuFirmware) hex_img = fu_firmware_new();
		g_autoptr(FuStructIlitekItsSkuItem) sku = NULL;
		g_autoptr(GInputStream) hex_stream = NULL;
		guint fw_size;
		guint fw_ver;
		guint fwid;
		g_autofree gchar *id = g_strdup_printf("sku:%u", i);

		sku = fu_struct_ilitek_its_sku_item_parse_stream(stream, offset, error);
		if (sku == NULL)
			return FALSE;

		fw_size = fu_struct_ilitek_its_sku_item_get_fw_size(sku);
		fw_ver = fu_struct_ilitek_its_sku_item_get_fw_ver(sku);
		fwid = fu_struct_ilitek_its_sku_item_get_fwid(sku);

		offset += FU_STRUCT_ILITEK_ITS_SKU_ITEM_OFFSET_FW_BUF;
		hex_stream = fu_partial_input_stream_new(stream, offset, fw_size, error);
		if (hex_stream == NULL)
			return FALSE;

		fu_firmware_set_idx(hex_img, fwid);
		fu_firmware_set_version_format(hex_img, FWUPD_VERSION_FORMAT_QUAD);
		fu_firmware_set_version_raw(hex_img, fw_ver);
		if (!fu_firmware_set_stream(hex_img, hex_stream, error))
			return FALSE;
		fu_firmware_add_image(firmware, hex_img);

		offset += fw_size;
	}

	return TRUE;
}

static gchar *
fu_ilitek_its_capsule_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_ilitek_its_convert_version(version_raw);
}

static void
fu_ilitek_its_capsule_firmware_init(FuIlitekItsCapsuleFirmware *self)
{
}

static void
fu_ilitek_its_capsule_firmware_class_init(FuIlitekItsCapsuleFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_ilitek_its_capsule_firmware_convert_version;
	firmware_class->validate = fu_ilitek_its_capsule_firmware_validate;
	firmware_class->parse = fu_ilitek_its_capsule_firmware_parse;
	firmware_class->export = fu_ilitek_its_capsule_firmware_export;
}

FuFirmware *
fu_ilitek_its_capsule_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ILITEK_ITS_CAPSULE_FIRMWARE, NULL));
}

guint8
fu_ilitek_its_capsule_firmware_get_lookup_cnt(FuIlitekItsCapsuleFirmware *self)
{
	return self->lookup_cnt;
}

guint8
fu_ilitek_its_capsule_firmware_get_lookup_type(FuIlitekItsCapsuleFirmware *self, guint8 idx)
{
	return self->lookups[idx].type;
}

guint16
fu_ilitek_its_capsule_firmware_get_lookup_fwid(FuIlitekItsCapsuleFirmware *self, guint8 idx)
{
	return self->lookups[idx].fwid;
}

guint32
fu_ilitek_its_capsule_firmware_get_lookup_edid(FuIlitekItsCapsuleFirmware *self, guint8 idx)
{
	return self->lookups[idx].edid;
}

guint32
fu_ilitek_its_capsule_firmware_get_lookup_edid_mask(FuIlitekItsCapsuleFirmware *self, guint8 idx)
{
	return self->lookups[idx].edid_mask;
}

guint8
fu_ilitek_its_capsule_firmware_get_lookup_sensor_id(FuIlitekItsCapsuleFirmware *self, guint8 idx)
{
	return self->lookups[idx].sensor_id;
}

guint8
fu_ilitek_its_capsule_firmware_get_lookup_sensor_id_mask(FuIlitekItsCapsuleFirmware *self,
							 guint8 idx)
{
	return self->lookups[idx].sensor_id_mask;
}
