/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-synaprom-firmware.h"

struct _FuSynapromFirmware {
	FuFirmware parent_instance;
	guint32 product_id;
};

G_DEFINE_TYPE(FuSynapromFirmware, fu_synaprom_firmware, FU_TYPE_FIRMWARE)

#define FU_SYNAPROM_FIRMWARE_TAG_MFW_HEADER  0x0001
#define FU_SYNAPROM_FIRMWARE_TAG_MFW_PAYLOAD 0x0002
#define FU_SYNAPROM_FIRMWARE_TAG_CFG_HEADER  0x0003
#define FU_SYNAPROM_FIRMWARE_TAG_CFG_PAYLOAD 0x0004

typedef struct __attribute__((packed)) {
	guint16 tag;
	guint32 bufsz;
} FuSynapromFirmwareHdr;

/* use only first 12 bit of 16 bits as tag value */
#define FU_SYNAPROM_FIRMWARE_TAG_MAX 0xfff0
#define FU_SYNAPROM_FIRMWARE_SIGSIZE 0x0100

#define FU_SYNAPROM_FIRMWARE_COUNT_MAX 64

static const gchar *
fu_synaprom_firmware_tag_to_string(guint16 tag)
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

static void
fu_synaprom_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuSynapromFirmware *self = FU_SYNAPROM_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "product_id", self->product_id);
}

static gboolean
fu_synaprom_firmware_parse(FuFirmware *firmware,
			   GBytes *fw,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint img_cnt = 0;

	g_return_val_if_fail(fw != NULL, FALSE);

	buf = g_bytes_get_data(fw, &bufsz);

	/* 256 byte signature as footer */
	if (bufsz < FU_SYNAPROM_FIRMWARE_SIGSIZE + sizeof(FuSynapromFirmwareHdr)) {
		g_set_error_literal(error,
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
		g_autoptr(FuFirmware) img = NULL;

		/* verify item header */
		memcpy(&header, buf, sizeof(header));
		tag = GUINT16_FROM_LE(header.tag);
		if (tag >= FU_SYNAPROM_FIRMWARE_TAG_MAX) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "tag 0x%04x is too large",
				    tag);
			return FALSE;
		}

		/* sanity check */
		img = fu_firmware_get_image_by_idx(firmware, tag, NULL);
		if (img != NULL) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "tag 0x%04x already present in image",
				    tag);
			return FALSE;
		}
		hdrsz = GUINT32_FROM_LE(header.bufsz);
		if (hdrsz == 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "empty header for tag 0x%04x",
				    tag);
			return FALSE;
		}
		offset += sizeof(header) + hdrsz;
		if (offset > bufsz) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "data is corrupted 0x%04x > 0x%04x",
				    (guint)offset,
				    (guint)bufsz);
			return FALSE;
		}

		/* move pointer to data */
		buf += sizeof(header);
		bytes = g_bytes_new(buf, hdrsz);
		g_debug("adding 0x%04x (%s) with size 0x%04x",
			tag,
			fu_synaprom_firmware_tag_to_string(tag),
			hdrsz);
		img = fu_firmware_new_from_bytes(bytes);
		fu_firmware_set_idx(img, tag);
		fu_firmware_set_id(img, fu_synaprom_firmware_tag_to_string(tag));
		fu_firmware_add_image(firmware, img);

		/* sanity check */
		if (img_cnt++ > FU_SYNAPROM_FIRMWARE_COUNT_MAX) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "maximum number of images exceeded, "
				    "maximum is 0x%02x",
				    (guint)FU_SYNAPROM_FIRMWARE_COUNT_MAX);
			return FALSE;
		}

		/* next item */
		buf += hdrsz;
	}
	return TRUE;
}

static GBytes *
fu_synaprom_firmware_write(FuFirmware *firmware, GError **error)
{
	FuSynapromFirmware *self = FU_SYNAPROM_FIRMWARE(firmware);
	GByteArray *blob = g_byte_array_new();
	g_autoptr(GBytes) payload = NULL;
	FuSynapromFirmwareMfwHeader hdr = {
	    .product = GUINT32_TO_LE(self->product_id),
	    .id = GUINT32_TO_LE(0xff),
	    .buildtime = GUINT32_TO_LE(0xff),
	    .buildnum = GUINT32_TO_LE(0xff),
	    .vmajor = 10,
	    .vminor = 1,
	};

	/* add header */
	fu_byte_array_append_uint16(blob, FU_SYNAPROM_FIRMWARE_TAG_MFW_HEADER, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(blob, sizeof(hdr), G_LITTLE_ENDIAN);
	g_byte_array_append(blob, (const guint8 *)&hdr, sizeof(hdr));

	/* add payload */
	payload = fu_firmware_get_bytes_with_patches(firmware, error);
	if (payload == NULL)
		return NULL;
	fu_byte_array_append_uint16(blob, FU_SYNAPROM_FIRMWARE_TAG_MFW_PAYLOAD, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(blob, g_bytes_get_size(payload), G_LITTLE_ENDIAN);
	fu_byte_array_append_bytes(blob, payload);

	/* add signature */
	for (guint i = 0; i < FU_SYNAPROM_FIRMWARE_SIGSIZE; i++)
		fu_byte_array_append_uint8(blob, 0xff);
	return g_byte_array_free_to_bytes(blob);
}

static gboolean
fu_synaprom_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuSynapromFirmware *self = FU_SYNAPROM_FIRMWARE(firmware);
	guint64 tmp;

	/* simple properties */
	tmp = xb_node_query_text_as_uint(n, "product_id", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		self->product_id = tmp;

	/* success */
	return TRUE;
}

static void
fu_synaprom_firmware_init(FuSynapromFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_synaprom_firmware_class_init(FuSynapromFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_synaprom_firmware_parse;
	klass_firmware->write = fu_synaprom_firmware_write;
	klass_firmware->export = fu_synaprom_firmware_export;
	klass_firmware->build = fu_synaprom_firmware_build;
}

FuFirmware *
fu_synaprom_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SYNAPROM_FIRMWARE, NULL));
}
