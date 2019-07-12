/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>

#include "fu-synaprom-firmware.h"

typedef struct __attribute__((packed)) {
	guint16			 tag;
	guint32			 bufsz;
} FuSynapromFirmwareHdr;

typedef struct {
	guint16			 tag;
	GBytes			*bytes;
} FuSynapromFirmwareItem;

/* use only first 12 bit of 16 bits as tag value */
#define FU_SYNAPROM_FIRMWARE_TAG_MAX			0xfff0
#define FU_SYNAPROM_FIRMWARE_SIGSIZE			0x0100

static void
fu_synaprom_firmware_item_free (FuSynapromFirmwareItem *item)
{
	g_bytes_unref (item->bytes);
	g_free (item);
}

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

GPtrArray *
fu_synaprom_firmware_new (GBytes *blob, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	gsize offset = 0;
	g_autoptr(GPtrArray) firmware = NULL;

	g_return_val_if_fail (blob != NULL, NULL);

	firmware = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_synaprom_firmware_item_free);
	buf = g_bytes_get_data (blob, &bufsz);

	/* 256 byte signature as footer */
	if (bufsz < FU_SYNAPROM_FIRMWARE_SIGSIZE + sizeof(FuSynapromFirmwareHdr)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "blob is too small to be firmware");
		return NULL;
	}
	bufsz -= FU_SYNAPROM_FIRMWARE_SIGSIZE;

	/* parse each chunk */
	while (offset != bufsz) {
		FuSynapromFirmwareHdr header;
		guint32 hdrsz;
		g_autofree FuSynapromFirmwareItem *item = NULL;

		/* verify item header */
		memcpy (&header, buf, sizeof(header));
		item = g_new0 (FuSynapromFirmwareItem, 1);
		item->tag = GUINT16_FROM_LE(header.tag);
		if (item->tag >= FU_SYNAPROM_FIRMWARE_TAG_MAX) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "tag 0x%04x is too large",
				     item->tag);
			return NULL;
		}
		hdrsz = GUINT32_FROM_LE(header.bufsz);
		offset += sizeof(header) + hdrsz;
		if (offset > bufsz) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "data is corrupted 0x%04x > 0x%04x",
				     (guint) offset, (guint) bufsz);
			return NULL;
		}

		/* move pointer to data */
		buf += sizeof(header);
		item->bytes = g_bytes_new (buf, hdrsz);
		g_debug ("adding 0x%04x (%s) with size 0x%04x",
			 item->tag,
			 fu_synaprom_firmware_tag_to_string (item->tag),
			 hdrsz);
		g_ptr_array_add (firmware, g_steal_pointer (&item));

		/* next item */
		buf += hdrsz;
	}
	return g_steal_pointer (&firmware);
}

GBytes *
fu_synaprom_firmware_get_bytes_by_tag (GPtrArray *firmware, guint16 tag, GError **error)
{
	for (guint i = 0; i < firmware->len; i++) {
		FuSynapromFirmwareItem *item = g_ptr_array_index (firmware, i);
		if (item->tag == tag)
			return g_bytes_ref (item->bytes);
	}
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_INVALID_ARGUMENT,
		     "no item with tag 0x%04x", tag);
	return NULL;
}

GBytes *
fu_synaprom_firmware_generate (void)
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
	guint16 tag1 = GUINT16_TO_LE(FU_SYNAPROM_FIRMWARE_TAG_MFW_HEADER);
	guint16 tag2 = GUINT16_TO_LE(FU_SYNAPROM_FIRMWARE_TAG_MFW_PAYLOAD);
	guint32 hdrsz = GUINT32_TO_LE(sizeof(hdr));
	guint32 datasz = GUINT32_TO_LE(sizeof(data));

	/* add header */
	g_byte_array_append (blob, (const guint8 *) &tag1, sizeof(tag1));
	g_byte_array_append (blob, (const guint8 *) &hdrsz, sizeof(hdrsz));
	g_byte_array_append (blob, (const guint8 *) &hdr, sizeof(hdr));

	/* add payload */
	g_byte_array_append (blob, (const guint8 *) &tag2, sizeof(tag2));
	g_byte_array_append (blob, (const guint8 *) &datasz, sizeof(datasz));
	g_byte_array_append (blob, data, sizeof(data));

	/* add signature */
	for (guint i = 0; i < FU_SYNAPROM_FIRMWARE_SIGSIZE; i++) {
		guint8 sig = 0xff;
		g_byte_array_append (blob, &sig, 1);
	}
	return g_byte_array_free_to_bytes (blob);
}
