/*
 * Copyright 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-nordic-hid-firmware-mcuboot.h"

#define IMAGE_MAGIC		  0x96f3b83d
#define IMAGE_TLV_INFO_MAGIC	  0x6907
#define IMAGE_TLV_PROT_INFO_MAGIC 0x6908

struct _FuNordicHidFirmwareMcuboot {
	FuNordicHidFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuNordicHidFirmwareMcuboot,
	      fu_nordic_hid_firmware_mcuboot,
	      FU_TYPE_NORDIC_HID_FIRMWARE)

static GByteArray *
fu_nordic_hid_firmware_mcuboot_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = fu_firmware_get_bytes_with_patches(firmware, error);

	if (blob == NULL)
		return NULL;

	/* https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/mcuboot/design.html#image-format
	 */
	fu_byte_array_append_uint32(buf, IMAGE_MAGIC, G_LITTLE_ENDIAN);
	/* load_addr */
	fu_byte_array_append_uint32(buf, 0x00, G_LITTLE_ENDIAN);
	/* hdr_size */
	fu_byte_array_append_uint16(buf, 0x20, G_LITTLE_ENDIAN);
	/* protect_tlv_size */
	fu_byte_array_append_uint16(buf, 0x00, G_LITTLE_ENDIAN);
	/* img_size */
	fu_byte_array_append_uint32(buf, (guint32)g_bytes_get_size(blob), G_LITTLE_ENDIAN);
	/* flags */
	fu_byte_array_append_uint32(buf, 0x00, G_LITTLE_ENDIAN);
	/* version */
	fu_byte_array_append_uint8(buf, 0x01);
	fu_byte_array_append_uint8(buf, 0x02);
	fu_byte_array_append_uint16(buf, 0x03, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x63, G_LITTLE_ENDIAN);
	/* pad */
	fu_byte_array_append_uint32(buf, 0xffffffff, G_LITTLE_ENDIAN);
	/* payload */
	fu_byte_array_append_bytes(buf, blob);
	/* TLV magic and total */
	fu_byte_array_append_uint16(buf, IMAGE_TLV_INFO_MAGIC, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, 0x00, G_LITTLE_ENDIAN);

	return g_steal_pointer(&buf);
}

/* simple validation of the image */
static gboolean
fu_nordic_hid_firmware_mcuboot_validate(FuFirmware *firmware, GInputStream *stream, GError **error)
{
	guint32 magic;
	guint16 hdr_size;
	guint32 img_size;
	guint8 ver_major;
	guint8 ver_minor;
	guint16 ver_rev;
	guint32 ver_build_nr;
	guint16 magic_tlv;
	g_autofree gchar *version = NULL;

	if (!fu_input_stream_read_u32(stream, 0, &magic, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (magic != IMAGE_MAGIC) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "incorrect image magic");
		return FALSE;
	}
	/* ignore load_addr */
	if (!fu_input_stream_read_u16(stream, 8, &hdr_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	/* ignore protect_tlv_size */
	if (!fu_input_stream_read_u32(stream, 12, &img_size, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* ignore TLVs themselves
	 * https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/mcuboot/design.html#protected-tlvs
	 * check the magic values only */
	if (!fu_input_stream_read_u16(stream,
				      hdr_size + img_size,
				      &magic_tlv,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (magic_tlv != IMAGE_TLV_INFO_MAGIC && magic_tlv != IMAGE_TLV_PROT_INFO_MAGIC) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "incorrect TLV info magic");
		return FALSE;
	}

	/* version */
	if (!fu_input_stream_read_u8(stream, 0x14, &ver_major, error))
		return FALSE;
	if (!fu_input_stream_read_u8(stream, 0x15, &ver_minor, error))
		return FALSE;
	if (!fu_input_stream_read_u16(stream, 0x16, &ver_rev, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_input_stream_read_u32(stream, 0x18, &ver_build_nr, G_LITTLE_ENDIAN, error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u.%u", ver_major, ver_minor, ver_rev, ver_build_nr);

	fu_firmware_set_version(firmware, version);

	return TRUE;
}

static gboolean
fu_nordic_hid_firmware_mcuboot_parse(FuFirmware *firmware,
				     GInputStream *stream,
				     FuFirmwareParseFlags flags,
				     GError **error)
{
	if (!FU_FIRMWARE_CLASS(fu_nordic_hid_firmware_mcuboot_parent_class)
		 ->parse(firmware, stream, flags, error))
		return FALSE;
	return fu_nordic_hid_firmware_mcuboot_validate(firmware, stream, error);
}

static void
fu_nordic_hid_firmware_mcuboot_init(FuNordicHidFirmwareMcuboot *self)
{
}

static void
fu_nordic_hid_firmware_mcuboot_class_init(FuNordicHidFirmwareMcubootClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_nordic_hid_firmware_mcuboot_parse;
	firmware_class->write = fu_nordic_hid_firmware_mcuboot_write;
}
