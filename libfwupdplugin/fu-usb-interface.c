/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 Emmanuel Pacaud <emmanuel@gnome.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * FuUsbInterface:
 *
 * This object is a thin glib wrapper around a libusb_interface_descriptor.
 *
 * All the data is copied when the object is created and the original
 * descriptor can be destroyed any at point.
 */

#include "config.h"

#include <string.h>

#include "fu-input-stream.h"
#include "fu-usb-endpoint-private.h"
#include "fu-usb-interface-private.h"

struct _FuUsbInterface {
	FuFirmware parent_instance;
	struct libusb_interface_descriptor iface;
	GBytes *extra;
	GPtrArray *endpoints; /* element-type FuUsbEndpoint */
};

static void
fu_usb_interface_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuUsbInterface,
		       fu_usb_interface,
		       FU_TYPE_FIRMWARE,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_usb_interface_codec_iface_init));

static gboolean
fu_usb_interface_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuUsbInterface *self = FU_USB_INTERFACE(codec);
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
	self->iface.bLength = json_object_get_int_member_with_default(json_object, "Length", 0x0);
	self->iface.bDescriptorType =
	    json_object_get_int_member_with_default(json_object, "DescriptorType", 0x0);
	self->iface.bInterfaceNumber =
	    json_object_get_int_member_with_default(json_object, "InterfaceNumber", 0x0);
	self->iface.bAlternateSetting =
	    json_object_get_int_member_with_default(json_object, "AlternateSetting", 0x0);
	self->iface.bInterfaceClass =
	    json_object_get_int_member_with_default(json_object, "InterfaceClass", 0x0);
	self->iface.bInterfaceSubClass =
	    json_object_get_int_member_with_default(json_object, "InterfaceSubClass", 0x0);
	self->iface.bInterfaceProtocol =
	    json_object_get_int_member_with_default(json_object, "InterfaceProtocol", 0x0);
	self->iface.iInterface =
	    json_object_get_int_member_with_default(json_object, "Interface", 0x0);

	/* array of endpoints */
	if (json_object_has_member(json_object, "UsbEndpoints")) {
		JsonArray *json_array = json_object_get_array_member(json_object, "UsbEndpoints");
		for (guint i = 0; i < json_array_get_length(json_array); i++) {
			JsonNode *node_tmp = json_array_get_element(json_array, i);
			g_autoptr(FuUsbEndpoint) endpoint =
			    g_object_new(FU_TYPE_USB_ENDPOINT, NULL);
			if (!fwupd_codec_from_json(FWUPD_CODEC(endpoint), node_tmp, error))
				return FALSE;
			g_ptr_array_add(self->endpoints, g_object_ref(endpoint));
		}
	}

	/* extra data */
	str = json_object_get_string_member_with_default(json_object, "ExtraData", NULL);
	if (str != NULL) {
		gsize bufsz = 0;
		g_autofree guchar *buf = g_base64_decode(str, &bufsz);
		if (self->extra != NULL)
			g_bytes_unref(self->extra);
		self->extra = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
	}

	/* success */
	return TRUE;
}

static void
fu_usb_interface_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuUsbInterface *self = FU_USB_INTERFACE(codec);

	/* optional properties */
	if (self->iface.bLength != 0) {
		json_builder_set_member_name(builder, "Length");
		json_builder_add_int_value(builder, self->iface.bLength);
	}
	if (self->iface.bDescriptorType != 0) {
		json_builder_set_member_name(builder, "DescriptorType");
		json_builder_add_int_value(builder, self->iface.bDescriptorType);
	}
	if (self->iface.bInterfaceNumber != 0) {
		json_builder_set_member_name(builder, "InterfaceNumber");
		json_builder_add_int_value(builder, self->iface.bInterfaceNumber);
	}
	if (self->iface.bAlternateSetting != 0) {
		json_builder_set_member_name(builder, "AlternateSetting");
		json_builder_add_int_value(builder, self->iface.bAlternateSetting);
	}
	if (self->iface.bInterfaceClass != 0) {
		json_builder_set_member_name(builder, "InterfaceClass");
		json_builder_add_int_value(builder, self->iface.bInterfaceClass);
	}
	if (self->iface.bInterfaceSubClass != 0) {
		json_builder_set_member_name(builder, "InterfaceSubClass");
		json_builder_add_int_value(builder, self->iface.bInterfaceSubClass);
	}
	if (self->iface.bInterfaceProtocol != 0) {
		json_builder_set_member_name(builder, "InterfaceProtocol");
		json_builder_add_int_value(builder, self->iface.bInterfaceProtocol);
	}
	if (self->iface.iInterface != 0) {
		json_builder_set_member_name(builder, "Interface");
		json_builder_add_int_value(builder, self->iface.iInterface);
	}

	/* array of endpoints */
	if (self->endpoints->len > 0) {
		json_builder_set_member_name(builder, "UsbEndpoints");
		json_builder_begin_array(builder);
		for (guint i = 0; i < self->endpoints->len; i++) {
			FuUsbEndpoint *endpoint = g_ptr_array_index(self->endpoints, i);
			json_builder_begin_object(builder);
			fwupd_codec_to_json(FWUPD_CODEC(endpoint), builder, flags);
			json_builder_end_object(builder);
		}
		json_builder_end_array(builder);
	}

	/* extra data */
	if (self->extra != NULL && g_bytes_get_size(self->extra) > 0) {
		g_autofree gchar *str = g_base64_encode(g_bytes_get_data(self->extra, NULL),
							g_bytes_get_size(self->extra));
		json_builder_set_member_name(builder, "ExtraData");
		json_builder_add_string_value(builder, str);
	}
}

/**
 * fu_usb_interface_new:
 *
 * Return value: a new #FuUsbInterface object.
 *
 * Since: 2.0.0
 **/
FuUsbInterface *
fu_usb_interface_new(const struct libusb_interface_descriptor *iface)
{
	FuUsbInterface *self = g_object_new(FU_TYPE_USB_INTERFACE, NULL);

	/* copy the data */
	memcpy(&self->iface, iface, sizeof(struct libusb_interface_descriptor));
	self->extra = g_bytes_new(iface->extra, iface->extra_length);
	for (guint i = 0; i < iface->bNumEndpoints; i++)
		g_ptr_array_add(self->endpoints, fu_usb_endpoint_new(&iface->endpoint[i]));

	return FU_USB_INTERFACE(self);
}

/**
 * fu_usb_interface_get_length:
 * @self: a #FuUsbInterface
 *
 * Gets the USB bus number for the interface.
 *
 * Return value: The 8-bit bus number
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_interface_get_length(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), 0);
	return self->iface.bLength;
}

/**
 * fu_usb_interface_get_kind:
 * @self: a #FuUsbInterface
 *
 * Gets the type of interface.
 *
 * Return value: a #FuUsbDescriptorKind
 *
 * Since: 2.0.0
 **/
FuUsbDescriptorKind
fu_usb_interface_get_kind(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), 0);
	return self->iface.bDescriptorType;
}

/**
 * fu_usb_interface_get_number:
 * @self: a #FuUsbInterface
 *
 * Gets the interface number.
 *
 * Return value: The interface ID
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_interface_get_number(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), 0);
	return self->iface.bInterfaceNumber;
}

/**
 * fu_usb_interface_get_alternate:
 * @self: a #FuUsbInterface
 *
 * Gets the alternate setting for the interface.
 *
 * Return value: alt setting, typically zero.
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_interface_get_alternate(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), 0);
	return self->iface.bAlternateSetting;
}

/**
 * fu_usb_interface_get_class:
 * @self: a #FuUsbInterface
 *
 * Gets the interface class, typically a #FuUsbInterfaceClassCode.
 *
 * Return value: a interface class number, e.g. 0x09 is a USB hub.
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_interface_get_class(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), 0);
	return self->iface.bInterfaceClass;
}

/**
 * fu_usb_interface_get_subclass:
 * @self: a #FuUsbInterface
 *
 * Gets the interface subclass qualified by the class number.
 * See fu_usb_interface_get_class().
 *
 * Return value: a interface subclass number.
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_interface_get_subclass(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), 0);
	return self->iface.bInterfaceSubClass;
}

/**
 * fu_usb_interface_get_protocol:
 * @self: a #FuUsbInterface
 *
 * Gets the interface protocol qualified by the class and subclass numbers.
 * See fu_usb_interface_get_class() and fu_usb_interface_get_subclass().
 *
 * Return value: a interface protocol number.
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_interface_get_protocol(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), 0);
	return self->iface.bInterfaceProtocol;
}

/**
 * fu_usb_interface_get_index:
 * @self: a #FuUsbInterface
 *
 * Gets the index for the string descriptor.
 *
 * Return value: a string descriptor index.
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_interface_get_index(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), 0);
	return self->iface.iInterface;
}

/**
 * fu_usb_interface_get_extra:
 * @self: a #FuUsbInterface
 *
 * Gets any extra data from the interface.
 *
 * Return value: (transfer none): a #GBytes, or %NULL for failure
 *
 * Since: 2.0.0
 **/
GBytes *
fu_usb_interface_get_extra(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), NULL);
	return self->extra;
}

/**
 * fu_usb_interface_get_endpoints:
 * @self: a #FuUsbInterface
 *
 * Gets interface endpoints.
 *
 * Return value: (transfer container) (element-type FuUsbEndpoint): an array of endpoints.
 *
 * Since: 2.0.0
 **/
GPtrArray *
fu_usb_interface_get_endpoints(FuUsbInterface *self)
{
	g_return_val_if_fail(FU_IS_USB_INTERFACE(self), NULL);
	return g_ptr_array_ref(self->endpoints);
}

static gboolean
fu_usb_interface_parse(FuFirmware *firmware,
		       GInputStream *stream,
		       gsize offset,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuUsbInterface *self = FU_USB_INTERFACE(firmware);
	gsize offset_start = offset;
	g_autoptr(FuUsbInterfaceHdr) st = NULL;

	/* parse as proper interface with endpoints */
	st = fu_usb_interface_hdr_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;
	self->iface.bLength = fu_usb_interface_hdr_get_length(st);
	self->iface.bDescriptorType = FU_USB_INTERFACE_HDR_DEFAULT_DESCRIPTOR_TYPE;
	self->iface.bInterfaceNumber = fu_usb_interface_hdr_get_interface_number(st);
	self->iface.bAlternateSetting = fu_usb_interface_hdr_get_alternate_setting(st);
	self->iface.bNumEndpoints = fu_usb_interface_hdr_get_num_endpoints(st);
	self->iface.bInterfaceClass = fu_usb_interface_hdr_get_interface_class(st);
	self->iface.bInterfaceSubClass = fu_usb_interface_hdr_get_interface_sub_class(st);
	self->iface.bInterfaceProtocol = fu_usb_interface_hdr_get_interface_protocol(st);
	self->iface.iInterface = fu_usb_interface_hdr_get_interface(st);

	/* extra data */
	if (self->iface.bLength > st->len) {
		self->extra = fu_input_stream_read_bytes(stream,
							 offset + st->len,
							 self->iface.bLength - st->len,
							 error);
		if (self->extra == NULL)
			return FALSE;
	}

	/* endpoints */
	offset += self->iface.bLength;
	for (guint i = 0; i < self->iface.bNumEndpoints; i++) {
		g_autoptr(FuUsbEndpoint) endpoint = g_object_new(FU_TYPE_USB_ENDPOINT, NULL);
		if (!fu_firmware_parse_stream(FU_FIRMWARE(endpoint),
					      stream,
					      offset,
					      FWUPD_INSTALL_FLAG_NONE,
					      error))
			return FALSE;
		offset += fu_firmware_get_size(FU_FIRMWARE(endpoint));
		g_ptr_array_add(self->endpoints, g_steal_pointer(&endpoint));
	}
	fu_firmware_set_size(FU_FIRMWARE(self), offset - offset_start);

	/* success */
	return TRUE;
}

static void
fu_usb_interface_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_usb_interface_add_json;
	iface->from_json = fu_usb_interface_from_json;
}

static void
fu_usb_interface_finalize(GObject *object)
{
	FuUsbInterface *self = FU_USB_INTERFACE(object);
	if (self->extra != NULL)
		g_bytes_unref(self->extra);
	g_ptr_array_unref(self->endpoints);
	G_OBJECT_CLASS(fu_usb_interface_parent_class)->finalize(object);
}

static void
fu_usb_interface_init(FuUsbInterface *self)
{
	self->endpoints = g_ptr_array_new_with_free_func(g_object_unref);
}

static void
fu_usb_interface_class_init(FuUsbInterfaceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_usb_interface_finalize;
	firmware_class->parse = fu_usb_interface_parse;
}
