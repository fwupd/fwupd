/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-usb-descriptor.h"

G_DEFINE_TYPE(FuUsbDescriptor, fu_usb_descriptor, FU_TYPE_FIRMWARE)

static gboolean
fu_usb_descriptor_parse(FuFirmware *firmware,
			GInputStream *stream,
			FuFirmwareParseFlags flags,
			GError **error)
{
	g_autoptr(FuUsbBaseHdr) st = NULL;

	/* parse */
	st = fu_usb_base_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	fu_firmware_set_size(firmware, fu_usb_base_hdr_get_length(st));
	fu_firmware_set_idx(firmware, fu_usb_base_hdr_get_descriptor_type(st));

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
