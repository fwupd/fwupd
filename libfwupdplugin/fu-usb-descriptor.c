/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-partial-input-stream.h"
#include "fu-usb-descriptor.h"

G_DEFINE_TYPE(FuUsbDescriptor, fu_usb_descriptor, FU_TYPE_FIRMWARE)

static gboolean
fu_usb_descriptor_parse(FuFirmware *firmware,
			GInputStream *stream,
			FuFirmwareParseFlags flags,
			GError **error)
{
	FuUsbDescriptor *self = FU_USB_DESCRIPTOR(firmware);
	g_autoptr(FuUsbBaseHdr) st = NULL;
	g_autoptr(GInputStream) stream_partial = NULL;

	/* parse */
	st = fu_usb_base_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	stream_partial =
	    fu_partial_input_stream_new(stream, 0x0, fu_usb_base_hdr_get_length(st), error);
	if (stream_partial == NULL) {
		g_prefix_error(error, "failed to cut USB descriptor: ");
		return FALSE;
	}
	if (!fu_firmware_set_stream(firmware, stream_partial, error))
		return FALSE;
	fu_firmware_set_idx(FU_FIRMWARE(self), fu_usb_base_hdr_get_descriptor_type(st));

	/* success */
	return TRUE;
}

static void
fu_usb_descriptor_class_init(FuUsbDescriptorClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_usb_descriptor_parse;
}

static void
fu_usb_descriptor_init(FuUsbDescriptor *self)
{
}
