/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * FuUsbHidDescriptor:
 *
 * This object is a placeholder for the HID descriptor, and is populated with data after the
 * device has been opened.
 */

#include "config.h"

#include "fu-usb-hid-descriptor-private.h"

struct _FuUsbHidDescriptor {
	FuUsbDescriptor parent_instance;
	guint8 iface_number;
	gsize descriptor_length;
	GBytes *blob;
};

static void
fu_usb_hid_descriptor_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuUsbHidDescriptor,
		       fu_usb_hid_descriptor,
		       FU_TYPE_USB_DESCRIPTOR,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
					     fu_usb_hid_descriptor_codec_iface_init));

static gboolean
fu_usb_hid_descriptor_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuUsbHidDescriptor *self = FU_USB_HID_DESCRIPTOR(codec);
	const gchar *tmp;

	tmp = json_node_get_string(json_node);
	if (tmp != NULL) {
		gsize bufsz = 0;
		g_autofree guchar *buf = g_base64_decode(tmp, &bufsz);
		g_autoptr(GBytes) blob = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
		fu_usb_hid_descriptor_set_blob(self, blob);
	}

	/* success */
	return TRUE;
}

static void
fu_usb_hid_descriptor_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuUsbHidDescriptor *self = FU_USB_HID_DESCRIPTOR(codec);
	g_autofree gchar *str = NULL;

	if (self->blob == NULL)
		return;
	str = g_base64_encode(g_bytes_get_data(self->blob, NULL), g_bytes_get_size(self->blob));
	json_builder_add_string_value(builder, str);
}

/**
 * fu_usb_hid_descriptor_get_iface_number:
 * @self: a #FuUsbHidDescriptor
 *
 * Gets the hid descriptor interface number.
 *
 * Return value: integer
 *
 * Since: 2.0.2
 **/
guint8
fu_usb_hid_descriptor_get_iface_number(FuUsbHidDescriptor *self)
{
	g_return_val_if_fail(FU_IS_USB_HID_DESCRIPTOR(self), 0);
	return self->iface_number;
}

/**
 * fu_usb_hid_descriptor_set_iface_number:
 * @self: a #FuUsbHidDescriptor
 *
 * Sets the hid descriptor interface number.
 *
 * Since: 2.0.2
 **/
void
fu_usb_hid_descriptor_set_iface_number(FuUsbHidDescriptor *self, guint8 iface_number)
{
	g_return_if_fail(FU_IS_USB_HID_DESCRIPTOR(self));
	self->iface_number = iface_number;
}

/**
 * fu_usb_hid_descriptor_get_descriptor_length:
 * @self: a #FuUsbHidDescriptor
 *
 * Gets the HID descriptor length.
 *
 * Return value: integer
 *
 * Since: 2.0.2
 **/
gsize
fu_usb_hid_descriptor_get_descriptor_length(FuUsbHidDescriptor *self)
{
	g_return_val_if_fail(FU_IS_USB_HID_DESCRIPTOR(self), 0);
	return self->descriptor_length;
}

/**
 * fu_usb_hid_descriptor_get_blob:
 * @self: a #FuUsbHidDescriptor
 *
 * Gets the HID descriptor binary blob.
 *
 * Return value: (transfer none): The descriptor data
 *
 * Since: 2.0.2
 **/
GBytes *
fu_usb_hid_descriptor_get_blob(FuUsbHidDescriptor *self)
{
	g_return_val_if_fail(FU_IS_USB_HID_DESCRIPTOR(self), NULL);
	return self->blob;
}

/**
 * fu_usb_hid_descriptor_set_blob:
 * @self: a #FuUsbHidDescriptor
 *
 * Sets the HID descriptor binary blob.
 *
 * Since: 2.0.2
 **/
void
fu_usb_hid_descriptor_set_blob(FuUsbHidDescriptor *self, GBytes *blob)
{
	g_return_if_fail(FU_IS_USB_HID_DESCRIPTOR(self));
	if (self->blob != NULL)
		g_bytes_unref(self->blob);
	self->blob = g_bytes_ref(blob);
}

static gboolean
fu_usb_hid_descriptor_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	FuUsbHidDescriptor *self = FU_USB_HID_DESCRIPTOR(firmware);
	g_autoptr(FuUsbHidDescriptorHdr) st = NULL;

	/* parse */
	st = fu_usb_hid_descriptor_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	self->descriptor_length = fu_usb_hid_descriptor_hdr_get_class_descriptor_length(st);

	/* success */
	return TRUE;
}

static void
fu_usb_hid_descriptor_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_usb_hid_descriptor_add_json;
	iface->from_json = fu_usb_hid_descriptor_from_json;
}

static void
fu_usb_hid_descriptor_init(FuUsbHidDescriptor *self)
{
}

static void
fu_usb_hid_descriptor_finalize(GObject *object)
{
	FuUsbHidDescriptor *self = FU_USB_HID_DESCRIPTOR(object);
	if (self->blob != NULL)
		g_bytes_unref(self->blob);
	G_OBJECT_CLASS(fu_usb_hid_descriptor_parent_class)->finalize(object);
}

static void
fu_usb_hid_descriptor_class_init(FuUsbHidDescriptorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_usb_hid_descriptor_finalize;
	firmware_class->parse = fu_usb_hid_descriptor_parse;
}

/**
 * fu_usb_hid_descriptor_new:
 *
 * Return value: a new #FuUsbHidDescriptor object.
 *
 * Since: 2.0.2
 **/
FuUsbHidDescriptor *
fu_usb_hid_descriptor_new(void)
{
	return FU_USB_HID_DESCRIPTOR(g_object_new(FU_TYPE_USB_HID_DESCRIPTOR, NULL));
}
