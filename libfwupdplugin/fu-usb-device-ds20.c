/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-dump.h"
#include "fu-mem.h"
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

typedef struct __attribute__((packed)) {
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
	if (g_getenv("FWUPD_VERBOSE") != NULL)
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
	fwupd_guid_t guid = {0x0};
	g_autofree gchar *guid_str = NULL;

	/* parse GUID */
	if (!fu_memcpy_safe((guint8 *)&guid,
			    sizeof(guid),
			    0x0, /* dst */
			    g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw),
			    offset + 0x1, /* src */
			    sizeof(guid),
			    error))
		return FALSE;

	/* matches the correct UUID */
	guid_str = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
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
	const guint8 *buf;
	gsize bufsz = 0;
	guint version_lowest = fu_firmware_get_version_raw(firmware);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) dsinfos = g_ptr_array_new_with_free_func(g_free);

	/* cut out the data */
	blob = fu_bytes_new_offset(fw,
				   1 + sizeof(fwupd_guid_t),
				   g_bytes_get_size(fw) - (1 + sizeof(fwupd_guid_t)),
				   error);
	if (blob == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob, &bufsz);
	for (gsize off = 0; off < bufsz; off += sizeof(FuUsbDeviceDs20Item)) {
		FuUsbDeviceDs20Item *dsinfo = g_new0(FuUsbDeviceDs20Item, 1);
		guint16 total_length = 0;
		guint32 platform_ver = 0;

		g_ptr_array_add(dsinfos, dsinfo);
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    off +
						G_STRUCT_OFFSET(FuUsbDeviceDs20Item, platform_ver),
					    &platform_ver,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    off +
						G_STRUCT_OFFSET(FuUsbDeviceDs20Item, total_length),
					    &total_length,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
					   off + G_STRUCT_OFFSET(FuUsbDeviceDs20Item, vendor_code),
					   &dsinfo->vendor_code,
					   error))
			return FALSE;
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
					   off + G_STRUCT_OFFSET(FuUsbDeviceDs20Item, alt_code),
					   &dsinfo->alt_code,
					   error))
			return FALSE;
		dsinfo->platform_ver = platform_ver;
		dsinfo->total_length = total_length;
		g_debug("PlatformVersion=0x%08x, TotalLength=0x%04x, VendorCode=0x%02x, "
			"AltCode=0x%02x",
			dsinfo->platform_ver,
			dsinfo->total_length,
			dsinfo->vendor_code,
			dsinfo->alt_code);
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
	fwupd_guid_t guid = {0x0};
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* bReserved */
	fu_byte_array_append_uint8(buf, 0x0);

	/* PlatformCapabilityUUID */
	if (!fwupd_guid_from_string(fu_firmware_get_id(firmware),
				    &guid,
				    FWUPD_GUID_FLAG_MIXED_ENDIAN,
				    error))
		return NULL;
	g_byte_array_append(buf, (const guint8 *)&guid, sizeof(guid));

	/* CapabilityData */
	fu_byte_array_append_uint32(buf, fu_firmware_get_version_raw(firmware), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, fu_firmware_get_size(firmware), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(buf, fu_firmware_get_idx(firmware));
	fu_byte_array_append_uint8(buf, 0x0); /* AltCode */

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_usb_device_ds20_init(FuUsbDeviceDs20 *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
}

static void
fu_usb_device_ds20_class_init(FuUsbDeviceDs20Class *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_usb_device_ds20_check_magic;
	klass_firmware->parse = fu_usb_device_ds20_parse;
	klass_firmware->write = fu_usb_device_ds20_write;
}
