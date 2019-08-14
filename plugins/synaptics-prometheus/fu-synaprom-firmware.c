/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>

#include "fu-common.h"
#include "fu-synaprom-firmware.h"

struct _FuSynapromFirmware {
	FuFirmware		 parent_instance;
};

G_DEFINE_TYPE (FuSynapromFirmware, fu_synaprom_firmware, FU_TYPE_FIRMWARE)

typedef struct __attribute__((packed)) {
	guint16			 tag;
	guint32			 bufsz;
} FuSynapromFirmwareHdr;

/* use only first 12 bit of 16 bits as tag value */
#define FU_SYNAPROM_FIRMWARE_TAG_MAX			0xfff0
#define FU_SYNAPROM_FIRMWARE_SIGSIZE			0x0100

static const gchar *
fu_synaprom_firmware_tag_to_string (guint16 tag)
{
	if (tag == FU_SYNAPROM_FIRMWARE_TAG_MFW_HEADER)
		return "mfw-update-header";
	if (tag == FU_SYNAPROM_FIRMWARE_TAG_MFW_PAYLOAD)
		return "mfw-update-payload";
	if (tag == FU_SYNAPROM_FIRMWARE_TAG_CFG_HEADER)
		return "cfg-update-header";
	if (tag == FU_SYNAPROM_FIRMWARE_TAG_CFG_PAYLOAD)
		return "cfg-update-payload";
	return NULL;
}

static gboolean
fu_synaprom_firmware_parse (FuFirmware *firmware,
			    GBytes *fw,
			    guint64 addr_start,
			    guint64 addr_end,
			    FwupdInstallFlags flags,
			    GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	gsize offset = 0;

	g_return_val_if_fail (fw != NULL, FALSE);

	buf = g_bytes_get_data (fw, &bufsz);

	/* 256 byte signature as footer */
	if (bufsz < FU_SYNAPROM_FIRMWARE_SIGSIZE + sizeof(FuSynapromFirmwareHdr)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "blob is too small to be firmware");
		return FALSE;
	}
	bufsz -= FU_SYNAPROM_FIRMWARE_SIGSIZE;

	/* parse each chunk */
	while (offset != bufsz) {
		FuSynapromFirmwareHdr header;
		guint32 hdrsz;
		guint32 tag;
		g_autoptr(GBytes) bytes = NULL;
		g_autoptr(FuFirmwareImage) img = NULL;

		/* verify item header */
		memcpy (&header, buf, sizeof(header));
		tag = GUINT16_FROM_LE(header.tag);
		if (tag >= FU_SYNAPROM_FIRMWARE_TAG_MAX) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "tag 0x%04x is too large",
				     tag);
			return FALSE;
		}
		hdrsz = GUINT32_FROM_LE(header.bufsz);
		offset += sizeof(header) + hdrsz;
		if (offset > bufsz) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "data is corrupted 0x%04x > 0x%04x",
				     (guint) offset, (guint) bufsz);
			return FALSE;
		}

		/* move pointer to data */
		buf += sizeof(header);
		bytes = g_bytes_new (buf, hdrsz);
		g_debug ("adding 0x%04x (%s) with size 0x%04x",
			 tag,
			 fu_synaprom_firmware_tag_to_string (tag),
			 hdrsz);
		img = fu_firmware_image_new (bytes);
		fu_firmware_image_set_idx (img, tag);
		fu_firmware_image_set_id (img, fu_synaprom_firmware_tag_to_string (tag));
		fu_firmware_add_image (firmware, img);

		/* next item */
		buf += hdrsz;
	}
	return TRUE;
}

static GBytes *
fu_synaprom_firmware_write (FuFirmware *self, GError **error)
{
	GByteArray *blob = g_byte_array_new ();
	const guint8 data[] = { 'R', 'H' };
	FuSynapromFirmwareMfwHeader hdr = {
		.product	= GUINT32_TO_LE(0x41),
		.id 		= GUINT32_TO_LE(0xff),
		.buildtime	= GUINT32_TO_LE(0xff),
		.buildnum	= GUINT32_TO_LE(0xff),
		.vmajor		= 10,
		.vminor		= 1,
	};

	/* add header */
	fu_byte_array_append_uint16 (blob, FU_SYNAPROM_FIRMWARE_TAG_MFW_HEADER, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (blob, sizeof(hdr), G_LITTLE_ENDIAN);
	g_byte_array_append (blob, (const guint8 *) &hdr, sizeof(hdr));

	/* add payload */
	fu_byte_array_append_uint16 (blob, FU_SYNAPROM_FIRMWARE_TAG_MFW_PAYLOAD, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32 (blob, sizeof(data), G_LITTLE_ENDIAN);
	g_byte_array_append (blob, data, sizeof(data));

	/* add signature */
	for (guint i = 0; i < FU_SYNAPROM_FIRMWARE_SIGSIZE; i++)
		fu_byte_array_append_uint8 (blob, 0xff);
	return g_byte_array_free_to_bytes (blob);
}

static void
fu_synaprom_firmware_init (FuSynapromFirmware *self)
{
}

static void
fu_synaprom_firmware_class_init (FuSynapromFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_synaprom_firmware_parse;
	klass_firmware->write = fu_synaprom_firmware_write;
}

FuFirmware *
fu_synaprom_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_SYNAPROM_FIRMWARE, NULL));
}
