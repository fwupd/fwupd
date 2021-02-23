/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-common-version.h"
#include "fu-pxi-firmware.h"

#define PIXART_RF_FW_HEADER_SIZE		32	/* bytes */
#define PIXART_RF_FW_HEADER_TAG_OFFSET		24

struct _FuPxiFirmware {
	FuFirmware		 parent_instance;
};

G_DEFINE_TYPE (FuPxiFirmware, fu_pxi_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_pxi_firmware_parse (FuFirmware *firmware,
		       GBytes *fw,
		       guint64 addr_start,
		       guint64 addr_end,
		       FwupdInstallFlags flags,
		       GError **error)
{
	const guint8 *buf;
	const guint8 tag[] = {
		0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
	};
	gboolean header_ok = TRUE;
	gsize bufsz = 0;
	guint32 version_raw = 0;
	guint8 fw_header[PIXART_RF_FW_HEADER_SIZE];
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* get buf */
	buf = g_bytes_get_data (fw, &bufsz);
	if (bufsz < sizeof(fw_header)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "firmware invalid, too small!");
		return FALSE;
	}

	/* get fw header */
	if (!fu_memcpy_safe (fw_header, sizeof(fw_header), 0x0,
			     buf, bufsz, bufsz - sizeof(fw_header),
			     sizeof(fw_header), error)) {
		g_prefix_error (error, "failed to read fw header ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "fw header",
				    fw_header, sizeof(fw_header));
	}

	/* check the tag from fw header is correct */
	for (guint32 i = 0x0; i < sizeof(tag); i++) {
		guint8 tmp = 0;
		if (!fu_common_read_uint8_safe (fw_header, sizeof(fw_header),
						i + PIXART_RF_FW_HEADER_TAG_OFFSET,
						&tmp, error))
			return FALSE;
		if (tmp != tag[i]) {
			header_ok = FALSE;
			break;
		}
	}

	/* set the default version if can not find it in fw bin */
	if (header_ok) {
		g_autofree gchar *version = NULL;
		version_raw = (((guint32) (fw_header[0] - '0')) << 16) +
			      (((guint32) (fw_header[2] - '0')) << 8) +
			        (guint32) (fw_header[4] - '0');
		fu_firmware_set_version_raw (firmware, version_raw);
		version = fu_common_version_from_uint32 (version_raw,
							 FWUPD_VERSION_FORMAT_DELL_BIOS);
		fu_firmware_set_version (firmware, version);
	} else {
		fu_firmware_set_version (firmware, "0.0.0");
	}

	/* success */
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static GBytes *
fu_pxi_firmware_write (FuFirmware *firmware, GError **error)
{
	guint8 fw_header[PIXART_RF_FW_HEADER_SIZE] = { 0x0 };
	guint64 version_raw = fu_firmware_get_version_raw (firmware);
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob = NULL;
	const guint8 tag[] = {
		0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
	};

	/* data first */
	blob = fu_firmware_get_image_default_bytes (firmware, error);
	if (blob == NULL)
		return NULL;
	buf = g_byte_array_sized_new (g_bytes_get_size (blob) + sizeof (fw_header));
	g_byte_array_append (buf,
			     g_bytes_get_data (blob, NULL),
			     g_bytes_get_size (blob));

	/* footer */
	if (!fu_memcpy_safe (fw_header, sizeof (fw_header),
			     PIXART_RF_FW_HEADER_TAG_OFFSET, /* dst */
			     tag, sizeof(tag), 0x0,	/* src */
			     sizeof(tag), error))
		return NULL;
	fw_header[0] = ((version_raw >> 16) & 0xff) + '0';
	fw_header[1] = '.';
	fw_header[2] = ((version_raw >> 8) & 0xff) + '0';
	fw_header[3] = '.';
	fw_header[4] = ((version_raw >> 0) & 0xff) + '0';
	if (!g_ascii_isdigit (fw_header[0]) ||
	    !g_ascii_isdigit (fw_header[2]) ||
	    !g_ascii_isdigit (fw_header[4])) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "cannot write invalid version number 0x%x",
			     (guint) version_raw);
		return NULL;
	}
	g_byte_array_append (buf, fw_header, sizeof(fw_header));
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static void
fu_pxi_firmware_init (FuPxiFirmware *self)
{
}

static void
fu_pxi_firmware_class_init (FuPxiFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_pxi_firmware_parse;
	klass_firmware->write = fu_pxi_firmware_write;
}

FuFirmware *
fu_pxi_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_PXI_FIRMWARE, NULL));
}
