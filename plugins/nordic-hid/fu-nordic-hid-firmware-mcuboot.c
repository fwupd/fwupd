/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

static GBytes *
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

	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

/* simple validation of the image */
static gboolean
fu_nordic_hid_firmware_mcuboot_validate(FuFirmware *firmware,
					guint8 const *buf,
					gsize bufsz,
					GError **error)
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

	if (!fu_memread_uint32_safe(buf, bufsz, 0, &magic, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (magic != IMAGE_MAGIC) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "incorrect image magic");
		return FALSE;
	}
	/* ignore load_addr */
	if (!fu_memread_uint16_safe(buf, bufsz, 8, &hdr_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	/* ignore protect_tlv_size */
	if (!fu_memread_uint32_safe(buf, bufsz, 12, &img_size, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* ignore TLVs themselves
	 * https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/mcuboot/design.html#protected-tlvs
	 * check the magic values only */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
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
	if (!fu_memread_uint8_safe(buf, bufsz, 0x14, &ver_major, error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf, bufsz, 0x15, &ver_minor, error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf, bufsz, 0x16, &ver_rev, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_memread_uint32_safe(buf, bufsz, 0x18, &ver_build_nr, G_LITTLE_ENDIAN, error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u.%u", ver_major, ver_minor, ver_rev, ver_build_nr);

	fu_firmware_set_version(firmware, version);

	return TRUE;
}

static gboolean
fu_nordic_hid_firmware_mcuboot_parse(FuFirmware *firmware,
				     GBytes *fw,
				     gsize offset,
				     FwupdInstallFlags flags,
				     GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;

	if (!FU_FIRMWARE_CLASS(fu_nordic_hid_firmware_mcuboot_parent_class)
		 ->parse(firmware, fw, offset, flags, error))
		return FALSE;

	buf = g_bytes_get_data(fw, &bufsz);
	if (buf == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unable to get the image binary");
		return FALSE;
	}

	return fu_nordic_hid_firmware_mcuboot_validate(firmware, buf, bufsz, error);
}

static void
fu_nordic_hid_firmware_mcuboot_init(FuNordicHidFirmwareMcuboot *self)
{
}

static void
fu_nordic_hid_firmware_mcuboot_class_init(FuNordicHidFirmwareMcubootClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_nordic_hid_firmware_mcuboot_parse;
	klass_firmware->write = fu_nordic_hid_firmware_mcuboot_write;
}

FuFirmware *
fu_nordic_hid_firmware_mcuboot_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_NORDIC_HID_FIRMWARE_MCUBOOT, NULL));
}
