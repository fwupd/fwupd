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
	struct libusb_bos_dev_capability_descriptor bos_cap;
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
	fu_xmlb_builder_insert_kv(
	    bn,
	    "dev_capability_type",
	    fu_usb_descriptor_kind_to_string(self->bos_cap.bDevCapabilityType));
}

static gboolean
fu_usb_bos_descriptor_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(firmware);
	const gchar *str;

	/* simple properties */
	str = xb_node_query_text(n, "dev_capability_type", NULL);
	if (str != NULL) {
		self->bos_cap.bDevCapabilityType = fu_usb_descriptor_kind_from_string(str);
		if (self->bos_cap.bDevCapabilityType == FU_USB_DESCRIPTOR_KIND_INVALID) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid DevCapabilityType %s",
				    str);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usb_bos_descriptor_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(codec);
	const gchar *str;
	JsonObject *json_object;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "not JSON object");
		return FALSE;
	}
	json_object = json_node_get_object(json_node);

	/* optional properties */
	self->bos_cap.bDevCapabilityType =
	    json_object_get_int_member_with_default(json_object, "DevCapabilityType", 0x0);

	/* data */
	str = json_object_get_string_member_with_default(json_object, "ExtraData", NULL);
	if (str != NULL) {
		gsize bufsz = 0;
		g_autofree guchar *buf = g_base64_decode(str, &bufsz);
		g_autoptr(GInputStream) stream = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();

		/* create child */
		stream = g_memory_input_stream_new_from_data(g_steal_pointer(&buf), bufsz, g_free);
		if (!fu_firmware_set_stream(img, stream, error))
			return FALSE;
		fu_firmware_set_id(img, FU_FIRMWARE_ID_PAYLOAD);
		if (!fu_firmware_add_image_full(FU_FIRMWARE(self), img, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_usb_bos_descriptor_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(codec);
	g_autoptr(GBytes) bytes = NULL;

	/* optional properties */
	if (self->bos_cap.bDevCapabilityType != 0) {
		json_builder_set_member_name(builder, "DevCapabilityType");
		json_builder_add_int_value(builder, self->bos_cap.bDevCapabilityType);
	}

	/* data */
	bytes = fu_firmware_get_image_by_id_bytes(FU_FIRMWARE(self), FU_FIRMWARE_ID_PAYLOAD, NULL);
	if (bytes != NULL && g_bytes_get_size(bytes) > 0) {
		g_autofree gchar *str =
		    g_base64_encode(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
		json_builder_set_member_name(builder, "ExtraData");
		json_builder_add_string_value(builder, str);
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
	return self->bos_cap.bDevCapabilityType;
}

static gboolean
fu_usb_bos_descriptor_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    FwupdInstallFlags flags,
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
	self->bos_cap.bLength = fu_usb_bos_hdr_get_length(st);
	self->bos_cap.bDevCapabilityType = fu_usb_bos_hdr_get_dev_capability_type(st);

	/* data */
	if (self->bos_cap.bLength > st->len) {
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(GInputStream) img_stream = NULL;

		img_stream = fu_partial_input_stream_new(stream,
							 st->len,
							 self->bos_cap.bLength - st->len,
							 error);
		if (img_stream == NULL)
			return FALSE;
		if (!fu_firmware_set_stream(img, img_stream, error))
			return FALSE;
		fu_firmware_set_id(img, FU_FIRMWARE_ID_PAYLOAD);
		if (!fu_firmware_add_image_full(FU_FIRMWARE(self), img, error))
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

	fu_usb_bos_hdr_set_dev_capability_type(st, self->bos_cap.bDevCapabilityType);
	blob = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, NULL);
	if (blob != NULL) {
		fu_byte_array_append_bytes(st, blob);
		fu_usb_bos_hdr_set_length(st, st->len);
	}

	/* success */
	return g_steal_pointer(&st);
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
}

/**
 * fu_usb_bos_descriptor_new:
 *
 * Return value: a new #FuUsbBosDescriptor object.
 *
 * Since: 2.0.0
 **/
FuUsbBosDescriptor *
fu_usb_bos_descriptor_new(const struct libusb_bos_dev_capability_descriptor *bos_cap)
{
	FuUsbBosDescriptor *self = g_object_new(FU_TYPE_USB_BOS_DESCRIPTOR, NULL);
	g_autoptr(FuFirmware) img = fu_firmware_new();
	g_autoptr(GBytes) bytes = NULL;

	/* copy the data */
	memcpy(&self->bos_cap, bos_cap, sizeof(*bos_cap)); /* nocheck:blocked */
	bytes = g_bytes_new(bos_cap->dev_capability_data, bos_cap->bLength - FU_USB_BOS_HDR_SIZE);
	fu_firmware_set_bytes(img, bytes);
	fu_firmware_set_id(img, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(FU_FIRMWARE(self), img);
	return FU_USB_BOS_DESCRIPTOR(self);
}
