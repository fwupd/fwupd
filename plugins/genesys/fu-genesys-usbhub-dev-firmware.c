/*
 * Copyright (C) 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-genesys-common.h"
#include "fu-genesys-usbhub-dev-firmware.h"
#include "fu-genesys-usbhub-firmware.h"
#include "fu-genesys-usbhub-struct.h"

struct _FuGenesysUsbhubDevFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuGenesysUsbhubDevFirmware, fu_genesys_usbhub_dev_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_genesys_usbhub_dev_firmware_check_magic(FuFirmware *firmware,
					   GBytes *fw,
					   gsize offset,
					   GError **error)
{
	guint8 magic[GENESYS_USBHUB_FW_SIG_LEN] = {0x0};

	if (!fu_memcpy_safe(magic,
			    sizeof(magic),
			    0, /* dst */
			    g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw),
			    offset + GENESYS_USBHUB_FW_SIG_OFFSET,
			    sizeof(magic),
			    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (memcmp(magic, GENESYS_USBHUB_FW_SIG_TEXT_DEV_BRIDGE, sizeof(magic)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "signature not supported");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_dev_firmware_parse(FuFirmware *firmware,
				     GBytes *fw,
				     gsize offset,
				     FwupdInstallFlags flags,
				     GError **error)
{
	guint32 code_size = 0;

	fu_firmware_set_id(firmware, fu_genesys_fw_type_to_string(FU_GENESYS_FW_TYPE_DEV_BRIDGE));
	fu_firmware_set_idx(firmware, FU_GENESYS_FW_TYPE_DEV_BRIDGE);
	fu_firmware_set_alignment(firmware, FU_FIRMWARE_ALIGNMENT_1K);

	/* deduce code size */
	if (!fu_genesys_usbhub_firmware_query_codesize(firmware, fw, offset, error)) {
		g_prefix_error(error, "not valid for dev: ");
		return FALSE;
	}
	code_size = fu_firmware_get_size(firmware);

	/* calculate checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_genesys_usbhub_firmware_verify(fw, offset, code_size, error)) {
			g_prefix_error(error, "not valid for dev: ");
			return FALSE;
		}
	}

	/* get firmware version */
	if (!fu_genesys_usbhub_firmware_query_version(firmware, fw, offset, error)) {
		g_prefix_error(error, "not valid for dev: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_genesys_usbhub_dev_firmware_init(FuGenesysUsbhubDevFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_genesys_usbhub_dev_firmware_class_init(FuGenesysUsbhubDevFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_genesys_usbhub_dev_firmware_check_magic;
	klass_firmware->parse = fu_genesys_usbhub_dev_firmware_parse;
}

FuFirmware *
fu_genesys_usbhub_dev_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GENESYS_USBHUB_DEV_FIRMWARE, NULL));
}
