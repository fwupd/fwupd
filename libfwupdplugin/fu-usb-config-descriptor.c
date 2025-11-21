/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * FuUsbConfigDescriptor:
 *
 * This object is a thin glib wrapper around a `libusb_config_dev_capability_descriptor`.
 *
 * All the data is copied when the object is created and the original descriptor can be destroyed
 * at any point.
 */

#include "config.h"

#include <string.h>

#include "fu-usb-config-descriptor-private.h"

struct _FuUsbConfigDescriptor {
	FuUsbDescriptor parent_instance;
	guint8 configuration;
	guint8 configuration_value;
};

static void
fu_usb_config_descriptor_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuUsbConfigDescriptor,
		       fu_usb_config_descriptor,
		       FU_TYPE_USB_DESCRIPTOR,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
					     fu_usb_config_descriptor_codec_iface_init));

static gboolean
fu_usb_config_descriptor_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuUsbConfigDescriptor *self = FU_USB_CONFIG_DESCRIPTOR(codec);
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
	self->configuration =
	    json_object_get_int_member_with_default(json_object, "Configuration", 0x0);
	self->configuration_value =
	    json_object_get_int_member_with_default(json_object, "ConfigurationValue", 0x0);

	/* success */
	return TRUE;
}

static void
fu_usb_config_descriptor_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuUsbConfigDescriptor *self = FU_USB_CONFIG_DESCRIPTOR(codec);

	/* optional properties */
	if (self->configuration != 0) {
		json_builder_set_member_name(builder, "Configuration");
		json_builder_add_int_value(builder, self->configuration);
	}
	if (self->configuration_value != 0) {
		json_builder_set_member_name(builder, "ConfigurationValue");
		json_builder_add_int_value(builder, self->configuration_value);
	}
}

/**
 * fu_usb_config_descriptor_get_configuration:
 * @self: a #FuUsbConfigDescriptor
 *
 * Gets the config descriptor configuration.
 *
 * Return value: integer
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_config_descriptor_get_configuration(FuUsbConfigDescriptor *self)
{
	g_return_val_if_fail(FU_IS_USB_CONFIG_DESCRIPTOR(self), 0);
	return self->configuration;
}

/**
 * fu_usb_config_descriptor_get_configuration_value:
 * @self: a #FuUsbConfigDescriptor
 *
 * Gets the CONFIG descriptor configuration value.
 *
 * Return value: integer
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_config_descriptor_get_configuration_value(FuUsbConfigDescriptor *self)
{
	g_return_val_if_fail(FU_IS_USB_CONFIG_DESCRIPTOR(self), 0);
	return self->configuration_value;
}

static gboolean
fu_usb_config_descriptor_parse(FuFirmware *firmware,
			       GInputStream *stream,
			       FuFirmwareParseFlags flags,
			       GError **error)
{
	FuUsbConfigDescriptor *self = FU_USB_CONFIG_DESCRIPTOR(firmware);
	g_autoptr(FuUsbDescriptorHdr) st = NULL;

	/* parse */
	st = fu_usb_descriptor_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	self->configuration = fu_usb_descriptor_hdr_get_configuration(st);
	self->configuration_value = fu_usb_descriptor_hdr_get_configuration_value(st);

	/* success */
	return TRUE;
}

static void
fu_usb_config_descriptor_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_usb_config_descriptor_add_json;
	iface->from_json = fu_usb_config_descriptor_from_json;
}

static void
fu_usb_config_descriptor_class_init(FuUsbConfigDescriptorClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_usb_config_descriptor_parse;
}

static void
fu_usb_config_descriptor_init(FuUsbConfigDescriptor *self)
{
}

/**
 * fu_usb_config_descriptor_new:
 *
 * Return value: a new #FuUsbConfigDescriptor object.
 *
 * Since: 2.0.0
 **/
FuUsbConfigDescriptor *
fu_usb_config_descriptor_new(void)
{
	return FU_USB_CONFIG_DESCRIPTOR(g_object_new(FU_TYPE_USB_CONFIG_DESCRIPTOR, NULL));
}
