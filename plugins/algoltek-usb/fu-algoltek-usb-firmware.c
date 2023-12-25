/*
 * Copyright (C) 2023 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-algoltek-usb-firmware.h"
#include "fu-algoltek-usb-struct.h"

struct _FuAlgoltekUsbFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekUsbFirmware, fu_algoltek_usb_firmware, FU_TYPE_FIRMWARE)

#define FU_ALGOLTEK_HEADER 0x4B45544C4F474C41 // ALGOLTEK

#define FU_ALGOLTEK_FIRMWARE_SIZE 0x20000
#define FU_ALGOLTEK_ISP_SIZE	  0x1000

static gboolean
fu_algoltek_usb_firmware_validate(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint64 magic = 0;
	/* First byte is header length */
	offset += 1;

	if (!fu_memread_uint64_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset,
				    &magic,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}

	if (magic != FU_ALGOLTEK_HEADER) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid magic, expected 0x%08lX got 0x%08lX",
			    (guint64)FU_ALGOLTEK_HEADER,
			    magic);
		return FALSE;
	}
	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_usb_firmware_parse(FuFirmware *firmware,
			       GBytes *fw,
			       gsize offset,
			       FwupdInstallFlags flags,
			       GError **error)
{
	g_autoptr(FuFirmware) img_ISP = fu_firmware_new();
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();
	g_autoptr(GBytes) blob_ISP = NULL;
	g_autoptr(GBytes) blob_payload = NULL;
	g_autoptr(GByteArray) headerArray = g_byte_array_new();
	g_autoptr(GBytes) blob_header = NULL;
	gchar *version;

	blob_header = fu_bytes_new_offset(fw, offset, 75, error);
	fu_byte_array_append_bytes(headerArray, blob_header);

	version = fu_struct_algoltek_product_identity_get_version(headerArray);

	/* len + Header = 9 bytes */
	offset += 9;
	/* len + ProductName = 17 bytes */
	offset += 17;
	/* len + FWVersion = 49 bytes */
	offset += 49;

	blob_ISP = fu_bytes_new_offset(fw, offset, FU_ALGOLTEK_ISP_SIZE, error);
	if (blob_ISP == NULL)
		return FALSE;
	fu_firmware_set_bytes(img_ISP, blob_ISP);
	fu_firmware_set_id(img_ISP, "ISP");
	fu_firmware_add_image(firmware, img_ISP);
	offset += g_bytes_get_size(blob_ISP);

	blob_payload = fu_bytes_new_offset(fw, offset, FU_ALGOLTEK_FIRMWARE_SIZE, error);
	if (blob_payload == NULL)
		return FALSE;

	fu_firmware_set_version(img_payload, version);
	fu_firmware_set_bytes(img_payload, blob_payload);
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, img_payload);
	return TRUE;
}

static GByteArray *
fu_algoltek_usb_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob_ISP = NULL;
	g_autoptr(GBytes) blob_payload = NULL;

	blob_ISP = fu_firmware_get_image_by_id_bytes(firmware, "ISP", error);
	if (blob_ISP == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_ISP);

	blob_payload = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (blob_payload == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_payload);

	return g_steal_pointer(&buf);
}

static void
fu_algoltek_usb_firmware_init(FuAlgoltekUsbFirmware *self)
{
}

static void
fu_algoltek_usb_firmware_class_init(FuAlgoltekUsbFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->validate = fu_algoltek_usb_firmware_validate;
	klass_firmware->parse = fu_algoltek_usb_firmware_parse;
	klass_firmware->write = fu_algoltek_usb_firmware_write;
}

FuFirmware *
fu_algoltek_usb_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ALGOLTEK_USB_FIRMWARE, NULL));
}
