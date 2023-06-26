/*
 * Copyright (C) 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-genesys-common.h"
#include "fu-genesys-usbhub-firmware.h"
#include "fu-genesys-usbhub-pd-firmware.h"
#include "fu-genesys-usbhub-struct.h"

struct _FuGenesysUsbhubPdFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuGenesysUsbhubPdFirmware, fu_genesys_usbhub_pd_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_genesys_usbhub_pd_firmware_check_magic(FuFirmware *firmware,
					  GBytes *fw,
					  gsize offset,
					  GError **error)
{
	guint8 magic[4] = "PRDY";
	return fu_memcmp_safe(g_bytes_get_data(fw, NULL),
			      g_bytes_get_size(fw),
			      offset + GENESYS_USBHUB_FW_SIG_OFFSET,
			      magic,
			      sizeof(magic),
			      0x0,
			      sizeof(magic),
			      error);
}

static gboolean
fu_genesys_usbhub_pd_firmware_parse(FuFirmware *firmware,
				    GBytes *fw,
				    gsize offset,
				    FwupdInstallFlags flags,
				    GError **error)
{
	guint32 code_size = 0;

	fu_firmware_set_id(firmware, fu_genesys_fw_type_to_string(FU_GENESYS_FW_TYPE_PD));
	fu_firmware_set_idx(firmware, FU_GENESYS_FW_TYPE_PD);
	fu_firmware_set_alignment(firmware, FU_FIRMWARE_ALIGNMENT_1K);

	/* deduce code size */
	if (!fu_genesys_usbhub_firmware_query_codesize(firmware, fw, offset, error)) {
		g_prefix_error(error, "not valid for pd: ");
		return FALSE;
	}
	code_size = fu_firmware_get_size(firmware);

	/* calculate checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_genesys_usbhub_firmware_verify(fw, offset, code_size, error)) {
			g_prefix_error(error, "not valid for pd: ");
			return FALSE;
		}
	}

	/* get firmware version */
	if (!fu_genesys_usbhub_firmware_query_version(firmware, fw, offset, error)) {
		g_prefix_error(error, "not valid for pd: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_genesys_usbhub_pd_firmware_init(FuGenesysUsbhubPdFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_genesys_usbhub_pd_firmware_class_init(FuGenesysUsbhubPdFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_genesys_usbhub_pd_firmware_check_magic;
	klass_firmware->parse = fu_genesys_usbhub_pd_firmware_parse;
}

FuFirmware *
fu_genesys_usbhub_pd_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GENESYS_USBHUB_PD_FIRMWARE, NULL));
}
