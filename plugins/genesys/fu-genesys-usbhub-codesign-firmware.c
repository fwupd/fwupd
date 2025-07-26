/*
 * Copyright 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-genesys-common.h"
#include "fu-genesys-usbhub-codesign-firmware.h"
#include "fu-genesys-usbhub-struct.h"

struct _FuGenesysUsbhubCodesignFirmware {
	FuFirmware parent_instance;
	FuGenesysFwCodesign codesign;
};

G_DEFINE_TYPE(FuGenesysUsbhubCodesignFirmware,
	      fu_genesys_usbhub_codesign_firmware,
	      FU_TYPE_FIRMWARE)

gint
fu_genesys_usbhub_codesign_firmware_get_codesign(FuGenesysUsbhubCodesignFirmware *self)
{
	g_return_val_if_fail(FU_IS_GENESYS_USBHUB_CODESIGN_FIRMWARE(self), 0);
	return self->codesign;
}

static gboolean
fu_genesys_usbhub_codesign_firmware_validate(FuFirmware *firmware,
					     GInputStream *stream,
					     gsize offset,
					     GError **error)
{
	gsize streamsz = 0;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz - offset != FU_STRUCT_GENESYS_FW_CODESIGN_INFO_RSA_SIZE &&
	    streamsz - offset != FU_STRUCT_GENESYS_FW_CODESIGN_INFO_ECDSA_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unknown codesign format");
		return FALSE;
	}
	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_codesign_firmware_parse(FuFirmware *firmware,
					  GInputStream *stream,
					  FuFirmwareParseFlags flags,
					  GError **error)
{
	FuGenesysUsbhubCodesignFirmware *self = FU_GENESYS_USBHUB_CODESIGN_FIRMWARE(firmware);
	gsize streamsz = 0;
	gsize code_size;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	code_size = streamsz;
	if (code_size == FU_STRUCT_GENESYS_FW_CODESIGN_INFO_RSA_SIZE) {
		if (!fu_struct_genesys_fw_codesign_info_rsa_validate_stream(stream, 0x0, error)) {
			g_prefix_error(error, "not valid for codesign: ");
			return FALSE;
		}
		self->codesign = FU_GENESYS_FW_CODESIGN_RSA;
	} else if (code_size == FU_STRUCT_GENESYS_FW_CODESIGN_INFO_ECDSA_SIZE) {
		if (!fu_struct_genesys_fw_codesign_info_ecdsa_validate_stream(stream, 0x0, error)) {
			g_prefix_error(error, "not valid for codesign: ");
			return FALSE;
		}
		self->codesign = FU_GENESYS_FW_CODESIGN_ECDSA;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unknown file format of size 0x%x",
			    (guint)streamsz);
		return FALSE;
	}

	fu_firmware_set_id(firmware, fu_genesys_fw_type_to_string(FU_GENESYS_FW_TYPE_CODESIGN));
	fu_firmware_set_idx(firmware, FU_GENESYS_FW_TYPE_CODESIGN);
	fu_firmware_set_size(firmware, code_size);

	return TRUE;
}

static void
fu_genesys_usbhub_codesign_firmware_export(FuFirmware *firmware,
					   FuFirmwareExportFlags flags,
					   XbBuilderNode *bn)
{
	FuGenesysUsbhubCodesignFirmware *self = FU_GENESYS_USBHUB_CODESIGN_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kv(bn, "codesign", fu_genesys_fw_codesign_to_string(self->codesign));
}

static void
fu_genesys_usbhub_codesign_firmware_init(FuGenesysUsbhubCodesignFirmware *self)
{
}

static void
fu_genesys_usbhub_codesign_firmware_class_init(FuGenesysUsbhubCodesignFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_genesys_usbhub_codesign_firmware_validate;
	firmware_class->parse = fu_genesys_usbhub_codesign_firmware_parse;
	firmware_class->export = fu_genesys_usbhub_codesign_firmware_export;
}
