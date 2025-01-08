/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUsbDeviceDs20"

#include "config.h"

#include "fu-input-stream.h"
#include "fu-usb-device-ds20-struct.h"
#include "fu-usb-device-ms-ds20.h"

struct _FuUsbDeviceMsDs20 {
	FuUsbDeviceDs20 parent_instance;
};

G_DEFINE_TYPE(FuUsbDeviceMsDs20, fu_usb_device_ms_ds20, FU_TYPE_USB_DEVICE_DS20)

static gboolean
fu_usb_device_ms_ds20_parse(FuUsbDeviceDs20 *self,
			    GInputStream *stream,
			    FuUsbDevice *device,
			    GError **error)
{
	gsize streamsz = 0;

	/* get length and type only */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	for (gsize offset = 0; offset < streamsz;) {
		guint16 desc_sz;
		guint16 desc_type;
		g_autoptr(FuStructMsDs20) st = NULL;

		st = fu_struct_ms_ds20_parse_stream(stream, offset, error);
		if (st == NULL)
			return FALSE;
		desc_sz = fu_struct_ms_ds20_get_size(st);
		if (desc_sz == 0)
			break;
		desc_type = fu_struct_ms_ds20_get_type(st);
		g_debug("USB OS descriptor type 0x%04x [%s], length 0x%04x",
			desc_type,
			fu_usb_device_ms_ds20_desc_to_string(desc_type),
			desc_sz);
		offset += desc_sz;
	}

	/* success */
	return TRUE;
}

static void
fu_usb_device_ms_ds20_class_init(FuUsbDeviceMsDs20Class *klass)
{
	FuUsbDeviceDs20Class *usb_device_ds20_class = FU_USB_DEVICE_DS20_CLASS(klass);
	usb_device_ds20_class->parse = fu_usb_device_ms_ds20_parse;
}

static void
fu_usb_device_ms_ds20_init(FuUsbDeviceMsDs20 *self)
{
	fu_firmware_set_version_raw(FU_FIRMWARE(self), 0x06030000); /* Windows 8.1 */
	fu_firmware_set_id(FU_FIRMWARE(self), "d8dd60df-4589-4cc7-9cd2-659d9e648a9f");
}

/**
 * fu_usb_device_ms_ds20_new:
 *
 * Creates a new #FuUsbDeviceMsDs20.
 *
 * Returns: (transfer full): a #FuFirmware
 *
 * Since: 1.8.5
 **/
FuFirmware *
fu_usb_device_ms_ds20_new(void)
{
	return g_object_new(FU_TYPE_USB_DEVICE_MS_DS20, NULL);
}
