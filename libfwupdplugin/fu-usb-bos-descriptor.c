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

#include "fu-usb-bos-descriptor-private.h"

struct _FuUsbBosDescriptor {
	GObject parent_instance;
	struct libusb_bos_dev_capability_descriptor bos_cap;
	GBytes *extra;
};

static void
fu_usb_bos_descriptor_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuUsbBosDescriptor,
		       fu_usb_bos_descriptor,
		       G_TYPE_OBJECT,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
					     fu_usb_bos_descriptor_codec_iface_init));

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
fu_usb_bos_descriptor_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(codec);

	/* optional properties */
	if (self->bos_cap.bDevCapabilityType != 0) {
		json_builder_set_member_name(builder, "DevCapabilityType");
		json_builder_add_int_value(builder, self->bos_cap.bDevCapabilityType);
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

/**
 * fu_usb_bos_descriptor_get_extra:
 * @self: a #FuUsbBosDescriptor
 *
 * Gets any extra data from the BOS descriptor.
 *
 * Return value: (transfer none): a #GBytes, or %NULL for failure
 *
 * Since: 2.0.0
 **/
GBytes *
fu_usb_bos_descriptor_get_extra(FuUsbBosDescriptor *self)
{
	g_return_val_if_fail(FU_IS_USB_BOS_DESCRIPTOR(self), NULL);
	return self->extra;
}

static void
fu_usb_bos_descriptor_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_usb_bos_descriptor_add_json;
	iface->from_json = fu_usb_bos_descriptor_from_json;
}

static void
fu_usb_bos_descriptor_finalize(GObject *object)
{
	FuUsbBosDescriptor *self = FU_USB_BOS_DESCRIPTOR(object);
	g_bytes_unref(self->extra);
	G_OBJECT_CLASS(fu_usb_bos_descriptor_parent_class)->finalize(object);
}

static void
fu_usb_bos_descriptor_class_init(FuUsbBosDescriptorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_usb_bos_descriptor_finalize;
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

	/* copy the data */
	memcpy(&self->bos_cap, bos_cap, sizeof(*bos_cap));
	self->extra = g_bytes_new(bos_cap->dev_capability_data, bos_cap->bLength - 0x03);
	return FU_USB_BOS_DESCRIPTOR(self);
}
