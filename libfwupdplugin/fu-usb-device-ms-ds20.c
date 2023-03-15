/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuUsbDeviceDs20"

#include "config.h"

#include "fu-struct.h"
#include "fu-usb-device-ms-ds20.h"

struct _FuUsbDeviceMsDs20 {
	FuUsbDeviceDs20 parent_instance;
};

G_DEFINE_TYPE(FuUsbDeviceMsDs20, fu_usb_device_ms_ds20, FU_TYPE_USB_DEVICE_DS20)

#define USB_OS_20_SET_HEADER_DESCRIPTOR	      0x00
#define USB_OS_20_SUBSET_HEADER_CONFIGURATION 0x01
#define USB_OS_20_SUBSET_HEADER_FUNCTION      0x02
#define USB_OS_20_FEATURE_COMPATIBLE_ID	      0x03
#define USB_OS_20_FEATURE_REG_PROPERTY	      0x04
#define USB_OS_20_FEATURE_MIN_RESUME_TIME     0x05
#define USB_OS_20_FEATURE_MODEL_ID	      0x06
#define USB_OS_20_FEATURE_CCGP_DEVICE	      0x07
#define USB_OS_20_FEATURE_VENDOR_REVISION     0x08

static const gchar *
fu_usb_device_os20_type_to_string(guint16 type)
{
	if (type == USB_OS_20_SET_HEADER_DESCRIPTOR)
		return "set-header-descriptor";
	if (type == USB_OS_20_SUBSET_HEADER_CONFIGURATION)
		return "subset-header-configuration";
	if (type == USB_OS_20_SUBSET_HEADER_FUNCTION)
		return "subset-header-function";
	if (type == USB_OS_20_FEATURE_COMPATIBLE_ID)
		return "feature-compatible-id";
	if (type == USB_OS_20_FEATURE_REG_PROPERTY)
		return "feature-reg-property";
	if (type == USB_OS_20_FEATURE_MIN_RESUME_TIME)
		return "feature-min-resume-time";
	if (type == USB_OS_20_FEATURE_MODEL_ID)
		return "feature-model-id";
	if (type == USB_OS_20_FEATURE_CCGP_DEVICE)
		return "feature-ccgp-device";
	if (type == USB_OS_20_FEATURE_VENDOR_REVISION)
		return "feature-vendor-revision";
	return NULL;
}

static gboolean
fu_usb_device_ms_ds20_parse(FuUsbDeviceDs20 *self,
			    GBytes *blob,
			    FuUsbDevice *device,
			    GError **error)
{
	FuStruct *st = fu_struct_lookup(self, "MsDs20Entry");
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);

	/* get length and type only */
	for (gsize offset = 0; offset < bufsz;) {
		guint16 desc_sz;
		guint16 desc_type;

		if (!fu_struct_unpack_full(st, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
			return FALSE;
		desc_sz = fu_struct_get_u16(st, "size");
		if (desc_sz == 0)
			break;
		desc_type = fu_struct_get_u16(st, "type");
		g_debug("USB OS descriptor type 0x%04x [%s], length 0x%04x",
			desc_type,
			fu_usb_device_os20_type_to_string(desc_type),
			desc_sz);
		offset += desc_sz;
	}

	/* success */
	return TRUE;
}

static void
fu_usb_device_ms_ds20_class_init(FuUsbDeviceMsDs20Class *klass)
{
	FuUsbDeviceDs20Class *usb_device_ds20_klass = FU_USB_DEVICE_DS20_CLASS(klass);
	usb_device_ds20_klass->parse = fu_usb_device_ms_ds20_parse;
}

static void
fu_usb_device_ms_ds20_init(FuUsbDeviceMsDs20 *self)
{
	fu_firmware_set_version_raw(FU_FIRMWARE(self), 0x06030000); /* Windows 8.1 */
	fu_firmware_set_id(FU_FIRMWARE(self), "d8dd60df-4589-4cc7-9cd2-659d9e648a9f");
	fu_struct_register(self,
			   "MsDs20Entry {"
			   "    size: u16le,"
			   "    type: u16le,"
			   "}");
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
