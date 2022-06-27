/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-nordic-hid-firmware-b0.h"

#define UPDATE_IMAGE_MAGIC_COMMON 0x281ee6de
#define UPDATE_IMAGE_MAGIC_FWINFO 0x8fcebb4c
#define UPDATE_IMAGE_MAGIC_NRF52  0x00003402
#define UPDATE_IMAGE_MAGIC_NRF53  0x00003502

struct _FuNordicHidFirmwareB0 {
	FuNordicHidFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuNordicHidFirmwareB0, fu_nordic_hid_firmware_b0, FU_TYPE_NORDIC_HID_FIRMWARE)

static GBytes *
fu_nordic_hid_firmware_b0_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;
	fu_byte_array_append_uint32(buf, UPDATE_IMAGE_MAGIC_COMMON, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, UPDATE_IMAGE_MAGIC_FWINFO, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, UPDATE_IMAGE_MAGIC_NRF52, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x00, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x00, G_LITTLE_ENDIAN);
	/* version */
	fu_byte_array_append_uint32(buf, 0x63, G_LITTLE_ENDIAN);
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob);
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_nordic_hid_firmware_b0_read_fwinfo(FuFirmware *firmware,
				      guint8 const *buf,
				      gsize bufsz,
				      GError **error)
{
	guint32 magic_common;
	guint32 magic_fwinfo;
	guint32 magic_compat;
	guint32 offset;
	guint32 hdr_offset[5] = {0x0000, 0x0200, 0x400, 0x800, 0x1000};
	guint8 ver_major = 0;
	guint8 ver_minor = 0;
	guint16 ver_rev = 0;
	guint32 ver_build_nr = 0;
	g_autofree gchar *version = NULL;

	/* find correct offset to fwinfo */
	for (guint32 i = 0; i < G_N_ELEMENTS(hdr_offset); i++) {
		offset = hdr_offset[i];
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset,
					    &magic_common,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset + 0x04,
					    &magic_fwinfo,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset + 0x08,
					    &magic_compat,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		/* version */
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset + 0x14,
					    &ver_build_nr,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;

		if (magic_common != UPDATE_IMAGE_MAGIC_COMMON ||
		    magic_fwinfo != UPDATE_IMAGE_MAGIC_FWINFO)
			continue;
		switch (magic_compat) {
		case UPDATE_IMAGE_MAGIC_NRF52:
		case UPDATE_IMAGE_MAGIC_NRF53:
			/* currently only the build number is saved into the image */
			version = g_strdup_printf("%u.%u.%u.%u",
						  ver_major,
						  ver_minor,
						  ver_rev,
						  ver_build_nr);
			fu_firmware_set_version(firmware, version);
			return TRUE;
		default:
			break;
		}
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unable to validate the update binary");
	return FALSE;
}

static gboolean
fu_nordic_hid_firmware_b0_parse(FuFirmware *firmware,
				GBytes *fw,
				gsize offset,
				FwupdInstallFlags flags,
				GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;

	if (!FU_FIRMWARE_CLASS(fu_nordic_hid_firmware_b0_parent_class)
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

	return fu_nordic_hid_firmware_b0_read_fwinfo(firmware, buf, bufsz, error);
}

static void
fu_nordic_hid_firmware_b0_init(FuNordicHidFirmwareB0 *self)
{
}

static void
fu_nordic_hid_firmware_b0_class_init(FuNordicHidFirmwareB0Class *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_nordic_hid_firmware_b0_parse;
	klass_firmware->write = fu_nordic_hid_firmware_b0_write;
}

FuFirmware *
fu_nordic_hid_firmware_b0_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_NORDIC_HID_FIRMWARE_B0, NULL));
}
