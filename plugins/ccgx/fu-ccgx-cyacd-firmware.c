/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-ccgx-cyacd-firmware.h"

struct _FuCcgxCyacdFirmware {
	FuFirmwareClass		 parent_instance;
	CyacdFileInfo		 cyacd_file_info_array[CYACD_HANDLE_MAX_COUNT];
	guint32			 cyacd_file_info_count;
	FWImageType		 fw_image_type;
	guint16			 silicon_id;
	guint16			 app_type;
};

G_DEFINE_TYPE (FuCcgxCyacdFirmware, fu_ccgx_cyacd_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_ccgx_cyacd_firmware_parse (FuFirmware *firmware,
			      GBytes *fw,
			      guint64 addr_start,
			      guint64 addr_end,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuCcgxCyacdFirmware *self = FU_CCGX_CYACD_FIRMWARE (firmware);
	CyacdFileHandle cyacd_handle_array[CYACD_HANDLE_MAX_COUNT] = {0};
	CyacdFileHandle* cyacd_handle = NULL;
	CyacdFileInfo cyacd_info = {0};
	guint32 handle_count = 0;
	guint32 index = 0;
	PDFWAppVersion fw_ver;
	gsize fw_size = 0;
	const guint8 *fw_buffer = g_bytes_get_data (fw, &fw_size);
	g_autofree gchar *fw_ver_str = NULL;
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	self->cyacd_file_info_count = 0;
	handle_count =  fu_ccgx_cyacd_file_init_handle (cyacd_handle_array,
							CYACD_HANDLE_MAX_COUNT,
							fw_buffer, fw_size);
	if (handle_count == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid cyacd firmware");
		return FALSE;
	}

	fw_ver.val = 0;
	for (index = 0; index < handle_count ; index++) {
		cyacd_handle = &cyacd_handle_array [index];

		/* parse cyacd data */
		if (!fu_ccgx_cyacd_file_parse (cyacd_handle, &cyacd_info)) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "cyacd parsing error");
			return FALSE;
		}

		if (fw_ver.val == 0 )
			fw_ver.val = cyacd_info.app_version.val;

		if (cyacd_info.silicon_id != self->silicon_id ||
		    cyacd_info.app_version.ver.type != self->app_type ) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
  					     FWUPD_ERROR_NOT_SUPPORTED,
					     "cyacd silicon id and app type mismatch");
			return FALSE;
		}

		memcpy (&self->cyacd_file_info_array [self->cyacd_file_info_count],
			&cyacd_info, sizeof(CyacdFileInfo));
		self->cyacd_file_info_count++;
	}

	fw_ver_str = g_strdup_printf ("%u.%u.%u",
				      (guint32) fw_ver.ver.major,
				      (guint32) fw_ver.ver.minor,
				      (guint32) fw_ver.ver.build);
	fu_firmware_set_version (firmware, fw_ver_str);
	/* whole image */
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_ccgx_cyacd_firmware_init (FuCcgxCyacdFirmware *self)
{
	self->cyacd_file_info_count = 0;
	self->silicon_id = 0;
	self->app_type = 0;
}

static void
fu_ccgx_cyacd_firmware_class_init (FuCcgxCyacdFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_ccgx_cyacd_firmware_parse;
}

FuFirmware *
fu_ccgx_cyacd_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_CCGX_CYACD_FIRMWARE, NULL));
}

void
fu_ccgx_cyacd_firmware_set_device_info (FuCcgxCyacdFirmware *self, FWImageType fw_image_type, guint16 silicon_id, guint16 app_type)
{
	g_return_if_fail (FU_IS_CCGX_CYACD_FIRMWARE (self));
	self->fw_image_type = fw_image_type;
	self->silicon_id = silicon_id;
	self->app_type = app_type;
}

guint32
fu_ccgx_cyacd_firmware_get_info_count (FuCcgxCyacdFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_CYACD_FIRMWARE (self), 0);
	return self->cyacd_file_info_count;
}

CyacdFileInfo *
fu_ccgx_cyacd_firmware_get_info_data (FuCcgxCyacdFirmware *self, guint32 index)
{
	g_return_val_if_fail (FU_IS_CCGX_CYACD_FIRMWARE (self), NULL);
	g_return_val_if_fail (index < CYACD_HANDLE_MAX_COUNT, NULL);
	return &self->cyacd_file_info_array [index];
}
