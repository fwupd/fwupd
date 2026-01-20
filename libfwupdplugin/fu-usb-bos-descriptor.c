/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * FuUsbBosDescriptor:
 *
 * This object is a thin glib wrapper around a `libusb_bos_dev_capability_descriptor`.
 *
 * All the data is copied when the object is created and the original descriptor can be destroyed
 * at any point.
 */

#include "config.h"

#include <string.h>

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-partial-input-stream.h"
#include "fu-usb-bos-descriptor-private.h"

struct _FuUsbBosDescriptor {
	FuUsbDescriptor parent_instance;
	guint8 length;
	guint8 dev_capability_type;
};

static void
fu_usb_bos_descriptor_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuUsbBosDescriptor,
		       fu_usb_bos_descriptor,
		       FU_TYPE_USB_DESCRIPTOR,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
					     fu_usb_bos_descriptor_codec_iface_init));

static void
fu_usb_bos_descriptor_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(firmware);
	fu_xmlb_builder_insert_kv(bn,
				  "dev_capability_type",
				  fu_usb_descriptor_kind_to_string(self->dev_capability_type));
}

static gboolean
fu_usb_bos_descriptor_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(firmware);
	const gchar *str;

	/* simple properties */
	str = xb_node_query_text(n, "dev_capability_type", NULL);
	if (str != NULL) {
		self->dev_capability_type = fu_usb_descriptor_kind_from_string(str);
		if (self->dev_capability_type == FU_USB_DESCRIPTOR_KIND_INVALID) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid dev_capability_type %s",
				    str);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usb_bos_descriptor_from_json(FwupdCodec *codec, FwupdJsonObject *json_obj, GError **error)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(codec);
	const gchar *str;
	gint64 tmpi = 0;

	/* optional properties */
	if (!fwupd_json_object_get_integer_with_default(json_obj,
							"DevCapabilityType",
							&tmpi,
							0x0,
							error))
		return FALSE;
	self->dev_capability_type = tmpi;

	/* data */
	str = fwupd_json_object_get_string(json_obj, "ExtraData", NULL);
	if (str != NULL) {
		gsize bufsz = 0;
		g_autofree guchar *buf = g_base64_decode(str, &bufsz);
		g_autoptr(GInputStream) stream = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();

		/* create child */
		stream = g_memory_input_stream_new_from_data(g_steal_pointer(&buf), bufsz, g_free);
		if (!fu_firmware_parse_stream(img,
					      stream,
					      0x0,
					      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
					      error))
			return FALSE;
		fu_firmware_set_id(img, FU_FIRMWARE_ID_PAYLOAD);
		if (!fu_firmware_add_image(FU_FIRMWARE(self), img, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_usb_bos_descriptor_add_json(FwupdCodec *codec, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(codec);
	g_autoptr(GBytes) bytes = NULL;

	/* optional properties */
	if (self->dev_capability_type != 0) {
		fwupd_json_object_add_integer(json_obj,
					      "DevCapabilityType",
					      self->dev_capability_type);
	}

	/* data */
	bytes = fu_firmware_get_image_by_id_bytes(FU_FIRMWARE(self), FU_FIRMWARE_ID_PAYLOAD, NULL);
	if (bytes != NULL && g_bytes_get_size(bytes) > 0) {
		g_autofree gchar *str =
		    g_base64_encode(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
		fwupd_json_object_add_string(json_obj, "ExtraData", str);
	}
}

/**
 * fu_usb_bos_descriptor_get_capability:
 * @self: a #FuUsbBosDescriptor
 *
 * Gets the BOS descriptor capability.
 *
 * Return value: capability
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_bos_descriptor_get_capability(FuUsbBosDescriptor *self)
{
	g_return_val_if_fail(FU_IS_USB_BOS_DESCRIPTOR(self), 0);
	return self->dev_capability_type;
}

static gboolean
fu_usb_bos_descriptor_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(firmware);
	g_autoptr(FuUsbBosHdr) st = NULL;

	/* FuUsbDescriptor */
	if (!FU_FIRMWARE_CLASS(fu_usb_bos_descriptor_parent_class)
		 ->parse(firmware, stream, flags, error))
		return FALSE;

	/* parse */
	st = fu_usb_bos_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	self->length = fu_usb_bos_hdr_get_length(st);
	self->dev_capability_type = fu_usb_bos_hdr_get_dev_capability_type(st);

	/* data */
	if (self->length > st->buf->len) {
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(GInputStream) img_stream = NULL;

		img_stream = fu_partial_input_stream_new(stream,
							 st->buf->len,
							 self->length - st->buf->len,
							 error);
		if (img_stream == NULL) {
			g_prefix_error_literal(error, "failed to cut BOS descriptor: ");
			return FALSE;
		}
		if (!fu_firmware_parse_stream(img,
					      img_stream,
					      0x0,
					      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
					      error))
			return FALSE;
		fu_firmware_set_id(img, FU_FIRMWARE_ID_PAYLOAD);
		if (!fu_firmware_add_image(FU_FIRMWARE(self), img, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_usb_bos_descriptor_write(FuFirmware *firmware, GError **error)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(firmware);
	g_autoptr(FuUsbBosHdr) st = fu_usb_bos_hdr_new();
	g_autoptr(GBytes) blob = NULL;

	fu_usb_bos_hdr_set_dev_capability_type(st, self->dev_capability_type);
	blob = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, NULL);
	if (blob != NULL)
		fu_byte_array_append_bytes(st->buf, blob);
	fu_usb_bos_hdr_set_length(st, st->buf->len);

	/* success */
	return g_steal_pointer(&st->buf);
}

static void
fu_usb_bos_descriptor_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_usb_bos_descriptor_add_json;
	iface->from_json = fu_usb_bos_descriptor_from_json;
}

static void
fu_usb_bos_descriptor_class_init(FuUsbBosDescriptorClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_usb_bos_descriptor_parse;
	firmware_class->write = fu_usb_bos_descriptor_write;
	firmware_class->build = fu_usb_bos_descriptor_build;
	firmware_class->export = fu_usb_bos_descriptor_export;
}

static void
fu_usb_bos_descriptor_init(FuUsbBosDescriptor *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALLOW_LINEAR);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
}

/**
 * fu_usb_bos_descriptor_new:
 * @st_hdr: a #FuUsbBosHdr
 *
 * Return value: a new #FuUsbBosDescriptor object.
 *
 * Since: 2.1.1
 **/
FuUsbBosDescriptor *
fu_usb_bos_descriptor_new(FuUsbBosHdr *st_hdr)
{
	g_autoptr(FuUsbBosDescriptor) self = g_object_new(FU_TYPE_USB_BOS_DESCRIPTOR, NULL);
	g_autoptr(FuFirmware) img = fu_firmware_new();
	g_autoptr(GBytes) bytes = NULL;

	/* copy the data */
	self->length = fu_usb_bos_hdr_get_length(st_hdr);
	self->dev_capability_type = fu_usb_bos_hdr_get_dev_capability_type(st_hdr);
	bytes = g_bytes_new(st_hdr->buf->data + FU_USB_BOS_HDR_SIZE,
			    st_hdr->buf->len - FU_USB_BOS_HDR_SIZE);
	fu_firmware_set_bytes(img, bytes);
	fu_firmware_set_id(img, FU_FIRMWARE_ID_PAYLOAD);
	if (!fu_firmware_add_image(FU_FIRMWARE(self), img, NULL))
		return NULL;
	return g_steal_pointer(&self);
}
