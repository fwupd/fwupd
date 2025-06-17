/*
 * Copyright 2020 Emmanuel Pacaud <emmanuel@gnome.org>
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * FuUsbEndpoint:
 *
 * This object is a thin glib wrapper around a libusb_endpoint_descriptor.
 *
 * All the data is copied when the object is created and the original
 * descriptor can be destroyed any at point.
 */

#include "config.h"

#include <string.h>

#include "fu-usb-endpoint-private.h"

struct _FuUsbEndpoint {
	FuUsbDescriptor parent_instance;
	struct libusb_endpoint_descriptor endpoint_descriptor;
};

static void
fu_usb_endpoint_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuUsbEndpoint,
		       fu_usb_endpoint,
		       FU_TYPE_USB_DESCRIPTOR,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_usb_endpoint_codec_iface_init));

static gboolean
fu_usb_endpoint_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuUsbEndpoint *self = FU_USB_ENDPOINT(codec);
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
	self->endpoint_descriptor.bDescriptorType =
	    json_object_get_int_member_with_default(json_object, "DescriptorType", 0x0);
	self->endpoint_descriptor.bEndpointAddress =
	    json_object_get_int_member_with_default(json_object, "EndpointAddress", 0x0);
	self->endpoint_descriptor.bRefresh =
	    json_object_get_int_member_with_default(json_object, "Refresh", 0x0);
	self->endpoint_descriptor.bInterval =
	    json_object_get_int_member_with_default(json_object, "Interval", 0x0);
	self->endpoint_descriptor.bSynchAddress =
	    json_object_get_int_member_with_default(json_object, "SynchAddress", 0x0);
	self->endpoint_descriptor.wMaxPacketSize =
	    json_object_get_int_member_with_default(json_object, "MaxPacketSize", 0x0);

	/* success */
	return TRUE;
}

static void
fu_usb_endpoint_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuUsbEndpoint *self = FU_USB_ENDPOINT(codec);

	/* optional properties */
	if (self->endpoint_descriptor.bDescriptorType != 0) {
		json_builder_set_member_name(builder, "DescriptorType");
		json_builder_add_int_value(builder, self->endpoint_descriptor.bDescriptorType);
	}
	if (self->endpoint_descriptor.bEndpointAddress != 0) {
		json_builder_set_member_name(builder, "EndpointAddress");
		json_builder_add_int_value(builder, self->endpoint_descriptor.bEndpointAddress);
	}
	if (self->endpoint_descriptor.bRefresh != 0) {
		json_builder_set_member_name(builder, "Refresh");
		json_builder_add_int_value(builder, self->endpoint_descriptor.bRefresh);
	}
	if (self->endpoint_descriptor.bInterval != 0) {
		json_builder_set_member_name(builder, "Interval");
		json_builder_add_int_value(builder, self->endpoint_descriptor.bInterval);
	}
	if (self->endpoint_descriptor.bSynchAddress != 0) {
		json_builder_set_member_name(builder, "SynchAddress");
		json_builder_add_int_value(builder, self->endpoint_descriptor.bSynchAddress);
	}
	if (self->endpoint_descriptor.wMaxPacketSize != 0) {
		json_builder_set_member_name(builder, "MaxPacketSize");
		json_builder_add_int_value(builder, self->endpoint_descriptor.wMaxPacketSize);
	}
}

/**
 * fu_usb_endpoint_get_maximum_packet_size:
 * @self: a #FuUsbEndpoint
 *
 * Gets the maximum packet size this endpoint is capable of sending/receiving.
 *
 * Return value: The maximum packet size
 *
 * Since: 2.0.0
 **/
guint16
fu_usb_endpoint_get_maximum_packet_size(FuUsbEndpoint *self)
{
	g_return_val_if_fail(FU_IS_USB_ENDPOINT(self), 0);
	return self->endpoint_descriptor.wMaxPacketSize;
}

/**
 * fu_usb_endpoint_get_polling_interval:
 * @self: a #FuUsbEndpoint
 *
 * Gets the endpoint polling interval.
 *
 * Return value: The endpoint polling interval
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_endpoint_get_polling_interval(FuUsbEndpoint *self)
{
	g_return_val_if_fail(FU_IS_USB_ENDPOINT(self), 0);
	return self->endpoint_descriptor.bInterval;
}

/**
 * fu_usb_endpoint_get_address:
 * @self: a #FuUsbEndpoint
 *
 * Gets the address of the endpoint.
 *
 * Return value: The 4-bit endpoint address
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_endpoint_get_address(FuUsbEndpoint *self)
{
	g_return_val_if_fail(FU_IS_USB_ENDPOINT(self), 0);
	return self->endpoint_descriptor.bEndpointAddress;
}

/**
 * fu_usb_endpoint_get_number:
 * @self: a #FuUsbEndpoint
 *
 * Gets the number part of endpoint address.
 *
 * Return value: The lower 4-bit of endpoint address
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_endpoint_get_number(FuUsbEndpoint *self)
{
	g_return_val_if_fail(FU_IS_USB_ENDPOINT(self), 0);
	return (self->endpoint_descriptor.bEndpointAddress) & 0xf;
}

/**
 * fu_usb_endpoint_get_direction:
 * @self: a #FuUsbEndpoint
 *
 * Gets the direction of the endpoint.
 *
 * Return value: The endpoint direction
 *
 * Since: 2.0.0
 **/
FuUsbDirection
fu_usb_endpoint_get_direction(FuUsbEndpoint *self)
{
	g_return_val_if_fail(FU_IS_USB_ENDPOINT(self), 0);
	return (self->endpoint_descriptor.bEndpointAddress & 0x80)
		   ? FU_USB_DIRECTION_DEVICE_TO_HOST
		   : FU_USB_DIRECTION_HOST_TO_DEVICE;
}

static gboolean
fu_usb_endpoint_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      FuFirmwareParseFlags flags,
		      GError **error)
{
	FuUsbEndpoint *self = FU_USB_ENDPOINT(firmware);
	g_autoptr(FuUsbEndpointHdr) st = NULL;

	/* FuUsbDescriptor */
	if (!FU_FIRMWARE_CLASS(fu_usb_endpoint_parent_class)->parse(firmware, stream, flags, error))
		return FALSE;

	/* parse */
	st = fu_usb_endpoint_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	self->endpoint_descriptor.bLength = fu_usb_endpoint_hdr_get_length(st);
	self->endpoint_descriptor.bDescriptorType = fu_usb_endpoint_hdr_get_descriptor_type(st);
	self->endpoint_descriptor.bEndpointAddress = fu_usb_endpoint_hdr_get_endpoint_address(st);
	self->endpoint_descriptor.bmAttributes = fu_usb_endpoint_hdr_get_attributes(st);
	self->endpoint_descriptor.wMaxPacketSize = fu_usb_endpoint_hdr_get_max_packet_size(st);
	self->endpoint_descriptor.bInterval = fu_usb_endpoint_hdr_get_interval(st);
	self->endpoint_descriptor.bRefresh = 0;
	self->endpoint_descriptor.bSynchAddress = 0;

	/* success */
	return TRUE;
}

static void
fu_usb_endpoint_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_usb_endpoint_add_json;
	iface->from_json = fu_usb_endpoint_from_json;
}

static void
fu_usb_endpoint_init(FuUsbEndpoint *self)
{
}

static void
fu_usb_endpoint_class_init(FuUsbEndpointClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_usb_endpoint_parse;
}

/**
 * fu_usb_endpoint_new:
 *
 * Return value: a new #FuUsbEndpoint object.
 *
 * Since: 2.0.0
 **/
FuUsbEndpoint *
fu_usb_endpoint_new(const struct libusb_endpoint_descriptor *endpoint_descriptor)
{
	FuUsbEndpoint *self = g_object_new(FU_TYPE_USB_ENDPOINT, NULL);

	/* copy the data */
	memcpy(&self->endpoint_descriptor, /* nocheck:blocked */
	       endpoint_descriptor,
	       sizeof(struct libusb_endpoint_descriptor));
	return FU_USB_ENDPOINT(self);
}
