/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bcm57xx-common.h"
#include "fu-bcm57xx-dict-image.h"

struct _FuBcm57xxDictImage {
	FuFirmware parent_instance;
	guint8 target;
	guint8 kind;
};

G_DEFINE_TYPE(FuBcm57xxDictImage, fu_bcm57xx_dict_image, FU_TYPE_FIRMWARE)

static void
fu_bcm57xx_dict_image_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuBcm57xxDictImage *self = FU_BCM57XX_DICT_IMAGE(firmware);
	if (self->target != 0xff)
		fu_xmlb_builder_insert_kx(bn, "target", self->target);
	if (self->kind != 0xff)
		fu_xmlb_builder_insert_kx(bn, "kind", self->kind);
}

static gboolean
fu_bcm57xx_dict_image_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	g_autoptr(GInputStream) stream_nocrc = NULL;
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < sizeof(guint32)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "dict image is too small");
		return FALSE;
	}
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_bcm57xx_verify_crc(stream, error))
			return FALSE;
	}
	stream_nocrc = fu_partial_input_stream_new(stream, 0x0, streamsz - sizeof(guint32), error);
	if (stream_nocrc == NULL)
		return FALSE;
	return fu_firmware_set_stream(firmware, stream_nocrc, error);
}

static GByteArray *
fu_bcm57xx_dict_image_write(FuFirmware *firmware, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint32 crc;
	g_autoptr(GByteArray) blob = NULL;
	g_autoptr(GBytes) fw_nocrc = NULL;

	/* get the CRC-less data */
	fw_nocrc = fu_firmware_get_bytes(firmware, error);
	if (fw_nocrc == NULL)
		return NULL;

	/* add to a mutable buffer */
	buf = g_bytes_get_data(fw_nocrc, &bufsz);
	blob = g_byte_array_sized_new(bufsz + sizeof(guint32));
	fu_byte_array_append_bytes(blob, fw_nocrc);

	/* add CRC */
	crc = fu_crc32(FU_CRC_KIND_B32_STANDARD, buf, bufsz);
	fu_byte_array_append_uint32(blob, crc, G_LITTLE_ENDIAN);
	return g_steal_pointer(&blob);
}

static gboolean
fu_bcm57xx_dict_image_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuBcm57xxDictImage *self = FU_BCM57XX_DICT_IMAGE(firmware);
	guint64 tmp;

	/* two simple properties */
	tmp = xb_node_query_text_as_uint(n, "kind", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		fu_bcm57xx_dict_image_set_kind(self, tmp);
	tmp = xb_node_query_text_as_uint(n, "target", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		fu_bcm57xx_dict_image_set_target(self, tmp);

	/* success */
	return TRUE;
}

static void
fu_bcm57xx_dict_image_ensure_id(FuBcm57xxDictImage *self)
{
	g_autofree gchar *id = NULL;
	struct {
		guint8 target;
		guint8 kind;
		const gchar *id;
	} ids[] = {{0x00, 0x00, "pxe"},
		   {0x0D, 0x00, "ape"},
		   {0x09, 0x00, "iscsi1"},
		   {0x05, 0x00, "iscsi2"},
		   {0x0b, 0x00, "iscsi3"},
		   {0x00, 0x01, "cfg1000"},
		   {0x04, 0x01, "vpd2"},
		   {0xff, 0xff, NULL}};
	if (self->target == 0xff || self->kind == 0xff)
		return;
	for (guint i = 0; ids[i].id != NULL; i++) {
		if (self->target == ids[i].target && self->kind == ids[i].kind) {
			g_debug("using %s for %02x:%02x", ids[i].id, self->target, self->kind);
			fu_firmware_set_id(FU_FIRMWARE(self), ids[i].id);
			return;
		}
	}
	id = g_strdup_printf("dict-%02x-%02x", self->target, self->kind);
	if (g_getenv("FWUPD_FUZZER_RUNNING") == NULL)
		g_warning("falling back to %s, please report", id);
	fu_firmware_set_id(FU_FIRMWARE(self), id);
}

void
fu_bcm57xx_dict_image_set_target(FuBcm57xxDictImage *self, guint8 target)
{
	self->target = target;
	fu_bcm57xx_dict_image_ensure_id(self);
}

guint8
fu_bcm57xx_dict_image_get_target(FuBcm57xxDictImage *self)
{
	return self->target;
}

void
fu_bcm57xx_dict_image_set_kind(FuBcm57xxDictImage *self, guint8 kind)
{
	self->kind = kind;
	fu_bcm57xx_dict_image_ensure_id(self);
}

guint8
fu_bcm57xx_dict_image_get_kind(FuBcm57xxDictImage *self)
{
	return self->kind;
}

static void
fu_bcm57xx_dict_image_init(FuBcm57xxDictImage *self)
{
	self->target = 0xff;
	self->kind = 0xff;
}

static void
fu_bcm57xx_dict_image_class_init(FuBcm57xxDictImageClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_bcm57xx_dict_image_parse;
	firmware_class->write = fu_bcm57xx_dict_image_write;
	firmware_class->build = fu_bcm57xx_dict_image_build;
	firmware_class->export = fu_bcm57xx_dict_image_export;
}

FuFirmware *
fu_bcm57xx_dict_image_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_BCM57XX_DICT_IMAGE, NULL));
}
