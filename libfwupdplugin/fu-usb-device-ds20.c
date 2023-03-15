/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuUsbDeviceDs20"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-dump.h"
#include "fu-struct.h"
#include "fu-usb-device-ds20.h"

/**
 * FuUsbDeviceDs20:
 *
 * A USB DS20 BOS descriptor.
 *
 * See also: [class@FuUsbDevice]
 */

typedef struct {
	guint32 version_lowest;
} FuUsbDeviceDs20Private;

G_DEFINE_TYPE_WITH_PRIVATE(FuUsbDeviceDs20, fu_usb_device_ds20, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_usb_device_ds20_get_instance_private(o))

typedef struct {
	guint32 platform_ver;
	guint16 total_length;
	guint8 vendor_code;
	guint8 alt_code;
} FuUsbDeviceDs20Item;

/**
 * fu_usb_device_ds20_set_version_lowest:
 * @self: a #FuUsbDeviceDs20
 * @version_lowest: version number
 *
 * Sets the lowest possible `platform_ver` for a DS20 descriptor.
 *
 * Since: 1.8.5
 **/
void
fu_usb_device_ds20_set_version_lowest(FuUsbDeviceDs20 *self, guint32 version_lowest)
{
	FuUsbDeviceDs20Private *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_USB_DEVICE_DS20(self));
	priv->version_lowest = version_lowest;
}

/**
 * fu_usb_device_ds20_apply_to_device:
 * @self: a #FuUsbDeviceDs20
 * @device: a #FuUsbDevice
 * @error: (nullable): optional return location for an error
 *
 * Sets the DS20 descriptor onto @device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.5
 **/
gboolean
fu_usb_device_ds20_apply_to_device(FuUsbDeviceDs20 *self, FuUsbDevice *device, GError **error)
{
#ifdef HAVE_GUSB
	FuUsbDeviceDs20Class *klass = FU_USB_DEVICE_DS20_GET_CLASS(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(device);
	gsize actual_length = 0;
	gsize total_length = fu_firmware_get_size(FU_FIRMWARE(self));
	guint8 vendor_code = fu_firmware_get_idx(FU_FIRMWARE(self));
	g_autofree guint8 *buf = g_malloc0(total_length);
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_USB_DEVICE_DS20(self), FALSE);
	g_return_val_if_fail(FU_IS_USB_DEVICE(device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   vendor_code, /* bRequest */
					   0x0,		/* wValue */
					   0x07,	/* wIndex */
					   buf,
					   total_length,
					   &actual_length,
					   500,
					   NULL, /* cancellable */
					   error)) {
		g_prefix_error(error, "requested vendor code 0x%02x: ", vendor_code);
		return FALSE;
	}
	if (total_length != actual_length) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "expected 0x%x bytes from vendor code 0x%02x, but got 0x%x",
			    (guint)total_length,
			    vendor_code,
			    (guint)actual_length);
		return FALSE;
	}

	/* debug */
	fu_dump_raw(G_LOG_DOMAIN, "PlatformCapabilityOs20", buf, actual_length);

	/* FuUsbDeviceDs20->parse */
	blob = g_bytes_new_take(g_steal_pointer(&buf), actual_length);
	return klass->parse(self, blob, device, error);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "GUsb support is unavailable");
	return FALSE;
#endif
}

static gboolean
fu_usb_device_ds20_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	FuStruct *st = fu_struct_lookup(firmware, "UsbDeviceDs20");
	g_autofree gchar *guid_str = NULL;

	/* matches the correct UUID */
	if (!fu_struct_unpack_full(st,
				   g_bytes_get_data(fw, NULL),
				   g_bytes_get_size(fw),
				   offset,
				   FU_STRUCT_FLAG_NONE,
				   error))
		return FALSE;
	guid_str =
	    fwupd_guid_to_string(fu_struct_get_guid(st, "guid"), FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (g_strcmp0(guid_str, fu_firmware_get_id(firmware)) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid UUID for DS20, expected %s",
			    fu_firmware_get_id(firmware));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gint
fu_usb_device_ds20_sort_by_platform_ver_cb(gconstpointer a, gconstpointer b)
{
	FuUsbDeviceDs20Item *ds1 = *((FuUsbDeviceDs20Item **)a);
	FuUsbDeviceDs20Item *ds2 = *((FuUsbDeviceDs20Item **)b);
	if (ds1->platform_ver < ds2->platform_ver)
		return -1;
	if (ds1->platform_ver > ds2->platform_ver)
		return 1;
	return 0;
}

static gboolean
fu_usb_device_ds20_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuUsbDeviceDs20 *self = FU_USB_DEVICE_DS20(firmware);
	FuUsbDeviceDs20Private *priv = GET_PRIVATE(self);
	FuStruct *st = fu_struct_lookup(self, "UsbDeviceDs20");
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	guint version_lowest = fu_firmware_get_version_raw(firmware);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) dsinfos = g_ptr_array_new_with_free_func(g_free);

	for (gsize off = 0; off < bufsz; off += fu_struct_size(st)) {
		g_autofree FuUsbDeviceDs20Item *dsinfo = g_new0(FuUsbDeviceDs20Item, 1);

		/* parse */
		if (!fu_struct_unpack_full(st, buf, bufsz, off, FU_STRUCT_FLAG_NONE, error))
			return FALSE;
		dsinfo->platform_ver = fu_struct_get_u32(st, "platform_ver");
		dsinfo->total_length = fu_struct_get_u16(st, "total_length");
		dsinfo->vendor_code = fu_struct_get_u8(st, "vendor_code");
		dsinfo->alt_code = fu_struct_get_u8(st, "alt_code");
		g_debug("PlatformVersion=0x%08x, TotalLength=0x%04x, VendorCode=0x%02x, "
			"AltCode=0x%02x",
			dsinfo->platform_ver,
			dsinfo->total_length,
			dsinfo->vendor_code,
			dsinfo->alt_code);
		g_ptr_array_add(dsinfos, g_steal_pointer(&dsinfo));
	}

	/* sort by platform_ver, highest first */
	g_ptr_array_sort(dsinfos, fu_usb_device_ds20_sort_by_platform_ver_cb);

	/* find the newest info that's not newer than the lowest version */
	for (guint i = 0; i < dsinfos->len; i++) {
		FuUsbDeviceDs20Item *dsinfo = g_ptr_array_index(dsinfos, i);

		/* not valid */
		if (dsinfo->platform_ver == 0x0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "invalid platform version 0x%08x",
				    dsinfo->platform_ver);
			return FALSE;
		}
		if (dsinfo->platform_ver < priv->version_lowest) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "invalid platform version 0x%08x, expected >= 0x%08x",
				    dsinfo->platform_ver,
				    priv->version_lowest);
			return FALSE;
		}

		/* dwVersion is effectively the minimum version */
		if (dsinfo->platform_ver <= version_lowest) {
			fu_firmware_set_size(firmware, dsinfo->total_length);
			fu_firmware_set_idx(firmware, dsinfo->vendor_code);
			return TRUE;
		}
	}

	/* failed */
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no supported platform version");
	return FALSE;
}

static GBytes *
fu_usb_device_ds20_write(FuFirmware *firmware, GError **error)
{
	FuStruct *st = fu_struct_lookup(firmware, "UsbDeviceDs20");
	fwupd_guid_t guid = {0x0};

	/* pack */
	if (!fwupd_guid_from_string(fu_firmware_get_id(firmware),
				    &guid,
				    FWUPD_GUID_FLAG_MIXED_ENDIAN,
				    error))
		return NULL;
	fu_struct_set_guid(st, "guid", &guid);
	fu_struct_set_u32(st, "platform_ver", fu_firmware_get_version_raw(firmware));
	fu_struct_set_u16(st, "total_length", fu_firmware_get_size(firmware));
	fu_struct_set_u8(st, "vendor_code", fu_firmware_get_idx(firmware));
	return fu_struct_pack_bytes(st);
}

static void
fu_usb_device_ds20_init(FuUsbDeviceDs20 *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_struct_register(self,
			   "UsbDeviceDs20 {"
			   "    reserved: u8,"
			   "    guid: guid,"
			   "    platform_ver: u32le,"
			   "    total_length: u16le,"
			   "    vendor_code: u8,"
			   "    alt_code: u8,"
			   "}");
}

static void
fu_usb_device_ds20_class_init(FuUsbDeviceDs20Class *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_usb_device_ds20_check_magic;
	klass_firmware->parse = fu_usb_device_ds20_parse;
	klass_firmware->write = fu_usb_device_ds20_write;
}
