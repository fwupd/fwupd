/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-synaptics-prometheus-firmware.h"
#include "fu-synaptics-prometheus-struct.h"

struct _FuSynapticsPrometheusFirmware {
	FuFirmware parent_instance;
	guint32 product_id;
	guint32 signature_size;
};

G_DEFINE_TYPE(FuSynapticsPrometheusFirmware, fu_synaptics_prometheus_firmware, FU_TYPE_FIRMWARE)

/* use only first 12 bit of 16 bits as tag value */
#define FU_SYNAPTICS_PROMETHEUS_FIRMWARE_TAG_MAX 0xfff0

#define FU_SYNAPTICS_PROMETHEUS_FIRMWARE_COUNT_MAX 64

guint32
fu_synaptics_prometheus_firmware_get_product_id(FuSynapticsPrometheusFirmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_PROMETHEUS_FIRMWARE(self), 0x0);
	return self->product_id;
}

gboolean
fu_synaptics_prometheus_firmware_set_signature_size(FuSynapticsPrometheusFirmware *self,
						    guint32 signature_size)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_PROMETHEUS_FIRMWARE(self), FALSE);
	self->signature_size = signature_size;
	return TRUE;
}

static void
fu_synaptics_prometheus_firmware_export(FuFirmware *firmware,
					FuFirmwareExportFlags flags,
					XbBuilderNode *bn)
{
	FuSynapticsPrometheusFirmware *self = FU_SYNAPTICS_PROMETHEUS_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "product_id", self->product_id);
}

static gboolean
fu_synaptics_prometheus_firmware_parse(FuFirmware *firmware,
				       GInputStream *stream,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	FuSynapticsPrometheusFirmware *self = FU_SYNAPTICS_PROMETHEUS_FIRMWARE(firmware);
	gsize offset = 0;
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < self->signature_size + FU_STRUCT_SYNAPTICS_PROMETHEUS_HDR_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "blob is too small to be firmware");
		return FALSE;
	}
	streamsz -= self->signature_size;

	/* parse each chunk */
	while (offset < streamsz) {
		guint32 hdrsz;
		guint32 tag;
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(FuFirmware) img_old = NULL;
		g_autoptr(FuStructSynapticsPrometheusHdr) st_hdr = NULL;
		g_autoptr(GInputStream) partial_stream = NULL;

		/* verify item header */
		st_hdr = fu_struct_synaptics_prometheus_hdr_parse_stream(stream, offset, error);
		if (st_hdr == NULL)
			return FALSE;
		tag = fu_struct_synaptics_prometheus_hdr_get_tag(st_hdr);
		if (tag >= FU_SYNAPTICS_PROMETHEUS_FIRMWARE_TAG_MAX) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "tag 0x%04x is too large",
				    tag);
			return FALSE;
		}

		/* sanity check */
		img_old = fu_firmware_get_image_by_idx(firmware, tag, NULL);
		if (img_old != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "tag 0x%04x already present in image",
				    tag);
			return FALSE;
		}
		hdrsz = fu_struct_synaptics_prometheus_hdr_get_bufsz(st_hdr);
		if (hdrsz == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "empty header for tag 0x%04x",
				    tag);
			return FALSE;
		}
		offset += st_hdr->buf->len;
		partial_stream = fu_partial_input_stream_new(stream, offset, hdrsz, error);
		if (partial_stream == NULL)
			return FALSE;
		if (!fu_firmware_parse_stream(img, partial_stream, 0x0, flags, error))
			return FALSE;
		g_debug("adding 0x%04x (%s) with size 0x%04x",
			tag,
			fu_synaptics_prometheus_firmware_tag_to_string(tag),
			hdrsz);
		fu_firmware_set_idx(img, tag);
		fu_firmware_set_id(img, fu_synaptics_prometheus_firmware_tag_to_string(tag));
		if (!fu_firmware_add_image(firmware, img, error))
			return FALSE;

		/* metadata */
		if (tag == FU_SYNAPTICS_PROMETHEUS_FIRMWARE_TAG_MFW_UPDATE_HEADER) {
			g_autofree gchar *version = NULL;
			g_autoptr(FuStructSynapticsPrometheusMfwHdr) st_mfw = NULL;
			st_mfw = fu_struct_synaptics_prometheus_mfw_hdr_parse_stream(stream,
										     offset,
										     error);
			if (st_mfw == NULL)
				return FALSE;
			self->product_id =
			    fu_struct_synaptics_prometheus_mfw_hdr_get_product(st_mfw);
			version = g_strdup_printf(
			    "%u.%u",
			    fu_struct_synaptics_prometheus_mfw_hdr_get_vmajor(st_mfw),
			    fu_struct_synaptics_prometheus_mfw_hdr_get_vminor(st_mfw));
			fu_firmware_set_version(firmware, version);
		}

		/* next item */
		offset += hdrsz;
	}
	return TRUE;
}

static GByteArray *
fu_synaptics_prometheus_firmware_write(FuFirmware *firmware, GError **error)
{
	FuSynapticsPrometheusFirmware *self = FU_SYNAPTICS_PROMETHEUS_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(FuStructSynapticsPrometheusHdr) st_hdr = fu_struct_synaptics_prometheus_hdr_new();
	g_autoptr(FuStructSynapticsPrometheusMfwHdr) st_mfw =
	    fu_struct_synaptics_prometheus_mfw_hdr_new();
	g_autoptr(GBytes) payload = NULL;

	/* add header */
	fu_struct_synaptics_prometheus_hdr_set_tag(
	    st_hdr,
	    FU_SYNAPTICS_PROMETHEUS_FIRMWARE_TAG_MFW_UPDATE_HEADER);
	fu_struct_synaptics_prometheus_hdr_set_bufsz(st_hdr, st_mfw->buf->len);
	g_byte_array_append(buf, st_hdr->buf->data, st_hdr->buf->len);
	fu_struct_synaptics_prometheus_mfw_hdr_set_product(st_mfw, self->product_id);
	fu_byte_array_append_array(buf, st_mfw->buf);

	/* add payload */
	payload = fu_firmware_get_bytes_with_patches(firmware, error);
	if (payload == NULL)
		return NULL;
	fu_struct_synaptics_prometheus_hdr_set_tag(
	    st_hdr,
	    FU_SYNAPTICS_PROMETHEUS_FIRMWARE_TAG_MFW_UPDATE_PAYLOAD);
	fu_struct_synaptics_prometheus_hdr_set_bufsz(st_hdr, g_bytes_get_size(payload));
	g_byte_array_append(buf, st_hdr->buf->data, st_hdr->buf->len);
	fu_byte_array_append_bytes(buf, payload);

	/* add signature */
	for (guint i = 0; i < self->signature_size; i++)
		fu_byte_array_append_uint8(buf, 0xff);
	return g_steal_pointer(&buf);
}

static gboolean
fu_synaptics_prometheus_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuSynapticsPrometheusFirmware *self = FU_SYNAPTICS_PROMETHEUS_FIRMWARE(firmware);
	guint64 tmp;

	/* simple properties */
	tmp = xb_node_query_text_as_uint(n, "product_id", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		self->product_id = tmp;

	/* success */
	return TRUE;
}

static void
fu_synaptics_prometheus_firmware_init(FuSynapticsPrometheusFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
	fu_firmware_set_images_max(FU_FIRMWARE(self), FU_SYNAPTICS_PROMETHEUS_FIRMWARE_COUNT_MAX);
	self->signature_size = FU_SYNAPTICS_PROMETHEUS_FIRMWARE_PROMETHEUS_SIGSIZE;
}

static void
fu_synaptics_prometheus_firmware_class_init(FuSynapticsPrometheusFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_synaptics_prometheus_firmware_parse;
	firmware_class->write = fu_synaptics_prometheus_firmware_write;
	firmware_class->export = fu_synaptics_prometheus_firmware_export;
	firmware_class->build = fu_synaptics_prometheus_firmware_build;
}

FuFirmware *
fu_synaptics_prometheus_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SYNAPTICS_PROMETHEUS_FIRMWARE, NULL));
}
