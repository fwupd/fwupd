/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-synaprom-firmware.h"
#include "fu-synaprom-struct.h"

struct _FuSynapromFirmware {
	FuFirmware parent_instance;
	guint32 product_id;
	guint32 signature_size;
};

G_DEFINE_TYPE(FuSynapromFirmware, fu_synaprom_firmware, FU_TYPE_FIRMWARE)

/* use only first 12 bit of 16 bits as tag value */
#define FU_SYNAPROM_FIRMWARE_TAG_MAX 0xfff0

#define FU_SYNAPROM_FIRMWARE_COUNT_MAX 64

guint32
fu_synaprom_firmware_get_product_id(FuSynapromFirmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPROM_FIRMWARE(self), 0x0);
	return self->product_id;
}

gboolean
fu_synaprom_firmware_set_signature_size(FuSynapromFirmware *self, guint32 signature_size)
{
	g_return_val_if_fail(FU_IS_SYNAPROM_FIRMWARE(self), FALSE);
	self->signature_size = signature_size;
	return TRUE;
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
	FuSynapromFirmware *self = FU_SYNAPROM_FIRMWARE(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (bufsz < self->signature_size + FU_STRUCT_SYNAPROM_HDR_SIZE) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "blob is too small to be firmware");
		return FALSE;
	}
	bufsz -= self->signature_size;

	/* parse each chunk */
	while (offset < bufsz) {
		guint32 hdrsz;
		guint32 tag;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GByteArray) st_hdr = NULL;
		g_autoptr(GBytes) bytes = NULL;

		/* verify item header */
		st_hdr = fu_struct_synaprom_hdr_parse(buf, bufsz, offset, error);
		if (st_hdr == NULL)
			return FALSE;
		tag = fu_struct_synaprom_hdr_get_tag(st_hdr);
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
		hdrsz = fu_struct_synaprom_hdr_get_bufsz(st_hdr);
		if (hdrsz == 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "empty header for tag 0x%04x",
				    tag);
			return FALSE;
		}
		offset += st_hdr->len;
		bytes = fu_bytes_new_offset(fw, offset, hdrsz, error);
		if (bytes == NULL)
			return FALSE;
		g_debug("adding 0x%04x (%s) with size 0x%04x",
			tag,
			fu_synaprom_firmware_tag_to_string(tag),
			hdrsz);
		img = fu_firmware_new_from_bytes(bytes);
		fu_firmware_set_idx(img, tag);
		fu_firmware_set_id(img, fu_synaprom_firmware_tag_to_string(tag));
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;

		/* metadata */
		if (tag == FU_SYNAPROM_FIRMWARE_TAG_MFW_UPDATE_HEADER) {
			g_autofree gchar *version = NULL;
			g_autoptr(GByteArray) st_mfw = NULL;
			st_mfw = fu_struct_synaprom_mfw_hdr_parse(buf, bufsz, offset, error);
			if (st_mfw == NULL)
				return FALSE;
			self->product_id = fu_struct_synaprom_mfw_hdr_get_product(st_mfw);
			version = g_strdup_printf("%u.%u",
						  fu_struct_synaprom_mfw_hdr_get_vmajor(st_mfw),
						  fu_struct_synaprom_mfw_hdr_get_vminor(st_mfw));
			fu_firmware_set_version(firmware, version);
		}

		/* next item */
		offset += hdrsz;
	}
	return TRUE;
}

static GByteArray *
fu_synaprom_firmware_write(FuFirmware *firmware, GError **error)
{
	FuSynapromFirmware *self = FU_SYNAPROM_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) st_hdr = fu_struct_synaprom_hdr_new();
	g_autoptr(GByteArray) st_mfw = fu_struct_synaprom_mfw_hdr_new();
	g_autoptr(GBytes) payload = NULL;

	/* add header */
	fu_struct_synaprom_hdr_set_tag(st_hdr, FU_SYNAPROM_FIRMWARE_TAG_MFW_UPDATE_HEADER);
	fu_struct_synaprom_hdr_set_bufsz(st_hdr, st_mfw->len);
	g_byte_array_append(buf, st_hdr->data, st_hdr->len);
	fu_struct_synaprom_mfw_hdr_set_product(st_mfw, self->product_id);
	g_byte_array_append(buf, st_mfw->data, st_mfw->len);

	/* add payload */
	payload = fu_firmware_get_bytes_with_patches(firmware, error);
	if (payload == NULL)
		return NULL;
	fu_struct_synaprom_hdr_set_tag(st_hdr, FU_SYNAPROM_FIRMWARE_TAG_MFW_UPDATE_PAYLOAD);
	fu_struct_synaprom_hdr_set_bufsz(st_hdr, g_bytes_get_size(payload));
	g_byte_array_append(buf, st_hdr->data, st_hdr->len);
	fu_byte_array_append_bytes(buf, payload);

	/* add signature */
	for (guint i = 0; i < self->signature_size; i++)
		fu_byte_array_append_uint8(buf, 0xff);
	return g_steal_pointer(&buf);
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
	fu_firmware_set_images_max(FU_FIRMWARE(self), FU_SYNAPROM_FIRMWARE_COUNT_MAX);
	self->signature_size = FU_SYNAPROM_FIRMWARE_PROMETHEUS_SIGSIZE;
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
