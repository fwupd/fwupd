/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-firmware.h"

/*
 * A zip archive carrying the `.bin` image and a detached signature over it.
 *
 * The signature-capable devices are shipped this layout on LVFS. While the
 * device firmware can accept a bare `.bin` for testing, the production
 * packages always use the archive format, so this parser only handles zip.
 *
 * The image is exported as the `payload` image, and the detached signature
 * as the `signature` image.
 */
/* the largest image these MCUs can store is a few hundred kB */
#define FU_LENOVO_ACCESSORY_FIRMWARE_SIZE_MAX (4 * 1024 * 1024)

struct _FuLenovoAccessoryFirmware {
	FuFirmware parent_instance;
	FuLenovoAccessorySignatureAlgo algo;
};

G_DEFINE_TYPE(FuLenovoAccessoryFirmware, fu_lenovo_accessory_firmware, FU_TYPE_FIRMWARE)

typedef struct {
	const gchar *glob;
	FuLenovoAccessorySignatureAlgo algo;
} FuLenovoAccessorySignatureGlob;

FuLenovoAccessorySignatureAlgo
fu_lenovo_accessory_firmware_get_algo(FuLenovoAccessoryFirmware *self)
{
	g_return_val_if_fail(FU_IS_LENOVO_ACCESSORY_FIRMWARE(self),
			     FU_LENOVO_ACCESSORY_SIGNATURE_ALGO_UNSIGNED);
	return self->algo;
}

static void
fu_lenovo_accessory_firmware_export(FuFirmware *firmware,
				    FuFirmwareExportFlags flags,
				    XbBuilderNode *bn)
{
	FuLenovoAccessoryFirmware *self = FU_LENOVO_ACCESSORY_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kv(bn,
				  "algo",
				  fu_lenovo_accessory_signature_algo_to_string(self->algo));
}

static gboolean
fu_lenovo_accessory_firmware_parse_zip(FuLenovoAccessoryFirmware *self,
				       FuInputStream *stream,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	g_autoptr(FuFirmware) firmware_zip = fu_zip_firmware_new();
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();
	g_autoptr(FuFirmware) img_sig = fu_firmware_new();
	g_autoptr(FuInputStream) stream_payload = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not an archive at all, or a malformed one */
	if (!fu_firmware_parse_stream(firmware_zip, stream, 0x0, flags, error))
		return FALSE;

	/* a valid zip has to carry the image */
	stream_payload = fu_firmware_get_image_by_id_stream(firmware_zip, "*.bin", &error_local);
	if (stream_payload == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "zip archive has no *.bin payload: %s",
			    error_local->message);
		return FALSE;
	}
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	if (!fu_firmware_parse_stream(img_payload, stream_payload, 0x0, flags, error))
		return FALSE;
	if (!fu_firmware_add_image(FU_FIRMWARE(self), img_payload, error))
		return FALSE;

	/*
	 * The detached signature, if any. Devices with a signature-capable
	 * bootloader still accept unsigned payloads, so a zip with no
	 * signature is not an error.
	 */
	{
		const FuLenovoAccessorySignatureGlob globs[] = {
		    {"*.rsa-3072.sig", FU_LENOVO_ACCESSORY_SIGNATURE_ALGO_RSA3072},
		};
		for (guint i = 0; i < G_N_ELEMENTS(globs); i++) {
			g_autoptr(FuInputStream) stream_sig = NULL;

			stream_sig =
			    fu_firmware_get_image_by_id_stream(firmware_zip, globs[i].glob, NULL);
			if (stream_sig == NULL)
				continue;
			fu_firmware_set_id(img_sig, FU_FIRMWARE_ID_SIGNATURE);
			if (!fu_firmware_parse_stream(img_sig, stream_sig, 0x0, flags, error))
				return FALSE;
			if (!fu_firmware_add_image(FU_FIRMWARE(self), img_sig, error))
				return FALSE;
			self->algo = globs[i].algo;
			break;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_firmware_parse(FuFirmware *firmware,
				   FuInputStream *stream,
				   FuFirmwareParseFlags flags,
				   GError **error)
{
	FuLenovoAccessoryFirmware *self = FU_LENOVO_ACCESSORY_FIRMWARE(firmware);
	return fu_lenovo_accessory_firmware_parse_zip(self, stream, flags, error);
}

static void
fu_lenovo_accessory_firmware_init(FuLenovoAccessoryFirmware *self)
{
	self->algo = FU_LENOVO_ACCESSORY_SIGNATURE_ALGO_UNSIGNED;
}

static void
fu_lenovo_accessory_firmware_class_init(FuLenovoAccessoryFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_set_size_max(firmware_class, FU_LENOVO_ACCESSORY_FIRMWARE_SIZE_MAX);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_FIRMWARE);
	firmware_class->parse = fu_lenovo_accessory_firmware_parse;
	firmware_class->export = fu_lenovo_accessory_firmware_export;
}

FuFirmware *
fu_lenovo_accessory_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LENOVO_ACCESSORY_FIRMWARE, NULL));
}
