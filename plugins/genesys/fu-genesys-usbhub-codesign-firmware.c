/*
 * Copyright (C) 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-genesys-common.h"
#include "fu-genesys-usbhub-codesign-firmware.h"
#include "fu-genesys-usbhub-struct.h"

struct _FuGenesysUsbhubCodesignFirmware {
	FuFirmwareClass parent_instance;
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
fu_genesys_usbhub_codesign_firmware_check_magic(FuFirmware *firmware,
						GBytes *fw,
						gsize offset,
						GError **error)
{
	gsize code_size = g_bytes_get_size(fw) - offset;

	if (code_size != FU_STRUCT_GENESYS_FW_CODESIGN_INFO_RSA_SIZE &&
	    code_size != FU_STRUCT_GENESYS_FW_CODESIGN_INFO_ECDSA_SIZE) {
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
					  GBytes *fw,
					  gsize offset,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuGenesysUsbhubCodesignFirmware *self = FU_GENESYS_USBHUB_CODESIGN_FIRMWARE(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	gsize code_size = bufsz - offset;

	if (code_size == FU_STRUCT_GENESYS_FW_CODESIGN_INFO_RSA_SIZE) {
		if (!fu_struct_genesys_fw_codesign_info_rsa_validate(buf, bufsz, offset, error)) {
			g_prefix_error(error, "not valid for codesign: ");
			return FALSE;
		}
		self->codesign = FU_GENESYS_FW_CODESIGN_RSA;
	} else if (code_size == FU_STRUCT_GENESYS_FW_CODESIGN_INFO_ECDSA_SIZE) {
		if (!fu_struct_genesys_fw_codesign_info_ecdsa_validate(buf, bufsz, offset, error)) {
			g_prefix_error(error, "not valid for codesign: ");
			return FALSE;
		}
		self->codesign = FU_GENESYS_FW_CODESIGN_ECDSA;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unknown file format at %#lx:%#lx",
			    offset,
			    bufsz);
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_genesys_usbhub_codesign_firmware_check_magic;
	klass_firmware->parse = fu_genesys_usbhub_codesign_firmware_parse;
	klass_firmware->export = fu_genesys_usbhub_codesign_firmware_export;
}

FuFirmware *
fu_genesys_usbhub_codesign_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GENESYS_USBHUB_CODESIGN_FIRMWARE, NULL));
}
