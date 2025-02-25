/*
 * Copyright 2024 Algoltek <Algoltek, Inc.>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-algoltek-aux-firmware.h"
#include "fu-algoltek-aux-struct.h"

struct _FuAlgoltekAuxFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekAuxFirmware, fu_algoltek_aux_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_algoltek_aux_firmware_validate(FuFirmware *firmware,
				  GInputStream *stream,
				  gsize offset,
				  GError **error)
{
	return fu_struct_algoltek_aux_product_identity_validate_stream(stream, offset, error);
}

static gboolean
fu_algoltek_aux_firmware_parse(FuFirmware *firmware,
			       GInputStream *stream,
			       FwupdInstallFlags flags,
			       GError **error)
{
	g_autofree gchar *version = NULL;
	gsize offset = 0;
	g_autoptr(FuFirmware) img_isp = fu_firmware_new();
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GInputStream) stream_isp = NULL;
	g_autoptr(GInputStream) stream_payload = NULL;

	/* identity */
	st = fu_struct_algoltek_aux_product_identity_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;
	version = fu_struct_algoltek_aux_product_identity_get_version(st);
	offset += FU_STRUCT_ALGOLTEK_AUX_PRODUCT_IDENTITY_SIZE;

	/* ISP */
	stream_isp =
	    fu_partial_input_stream_new(stream, offset, FU_ALGOLTEK_AUX_FIRMWARE_ISP_SIZE, error);
	if (stream_isp == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_isp, stream_isp, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(img_isp, "isp");
	fu_firmware_add_image(firmware, img_isp);
	offset += FU_ALGOLTEK_AUX_FIRMWARE_ISP_SIZE;

	/* payload */
	stream_payload = fu_partial_input_stream_new(stream,
						     offset,
						     FU_ALGOLTEK_AUX_FIRMWARE_PAYLOAD_SIZE,
						     error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_payload, stream_payload, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_version(img_payload, version);
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, img_payload);

	/* success */
	return TRUE;
}

static GByteArray *
fu_algoltek_aux_firmware_write(FuFirmware *firmware, GError **error)
{
	const gchar *product = fu_firmware_get_id(firmware);
	const gchar *version;
	g_autoptr(FuFirmware) img_payload = NULL;
	g_autoptr(FuStructAlgoltekAuxProductIdentity) st_id =
	    fu_struct_algoltek_aux_product_identity_new();
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob_isp = NULL;
	g_autoptr(GBytes) blob_isp_padded = NULL;
	g_autoptr(GBytes) blob_payload = NULL;
	g_autoptr(GBytes) blob_payload_padded = NULL;

	/* identity */
	img_payload = fu_firmware_get_image_by_id(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (img_payload == NULL)
		return NULL;
	if (product != NULL) {
		if (!fu_struct_algoltek_aux_product_identity_set_product_name(st_id,
									      product,
									      error))
			return NULL;
		fu_struct_algoltek_aux_product_identity_set_product_name_len(st_id,
									     strlen(product));
	}
	version = fu_firmware_get_version(img_payload);
	if (version != NULL) {
		if (!fu_struct_algoltek_aux_product_identity_set_version(st_id, version, error))
			return NULL;
		fu_struct_algoltek_aux_product_identity_set_version_len(st_id, strlen(version));
	}
	g_byte_array_append(buf, st_id->data, st_id->len);

	/* ISP */
	blob_isp = fu_firmware_get_image_by_id_bytes(firmware, "isp", error);
	if (blob_isp == NULL)
		return NULL;
	blob_isp_padded = fu_bytes_pad(blob_isp, FU_ALGOLTEK_AUX_FIRMWARE_ISP_SIZE, 0xFF);
	fu_byte_array_append_bytes(buf, blob_isp_padded);

	/* payload */
	blob_payload = fu_firmware_get_bytes(img_payload, error);
	if (blob_payload == NULL)
		return NULL;
	blob_payload_padded =
	    fu_bytes_pad(blob_payload, FU_ALGOLTEK_AUX_FIRMWARE_PAYLOAD_SIZE, 0xFF);
	fu_byte_array_append_bytes(buf, blob_payload_padded);

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_algoltek_aux_firmware_init(FuAlgoltekAuxFirmware *self)
{
}

static void
fu_algoltek_aux_firmware_class_init(FuAlgoltekAuxFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->validate = fu_algoltek_aux_firmware_validate;
	klass_firmware->parse = fu_algoltek_aux_firmware_parse;
	klass_firmware->write = fu_algoltek_aux_firmware_write;
}
