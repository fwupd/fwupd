/*
 * Copyright 2022 Intel
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-igsc-code-firmware.h"
#include "fu-igsc-struct.h"

struct _FuIgscCodeFirmware {
	FuIfwiFptFirmware parent_instance;
	guint32 hw_sku;
};

G_DEFINE_TYPE(FuIgscCodeFirmware, fu_igsc_code_firmware, FU_TYPE_IFWI_FPT_FIRMWARE)

#define GSC_FWU_IUP_NUM		  2
#define FU_IGSC_FIRMWARE_MAX_SIZE (8 * 1024 * 1024) /* 8M */

static void
fu_igsc_code_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIgscCodeFirmware *self = FU_IGSC_CODE_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "hw_sku", self->hw_sku);
}

guint32
fu_igsc_code_firmware_get_hw_sku(FuIgscCodeFirmware *self)
{
	g_return_val_if_fail(FU_IS_IFWI_FPT_FIRMWARE(self), G_MAXUINT32);
	return self->hw_sku;
}

static gboolean
fu_igsc_code_firmware_parse_imgi(FuIgscCodeFirmware *self, GInputStream *stream, GError **error)
{
	g_autoptr(GByteArray) st_inf = NULL;

	/* the command is only supported on DG2 */
	if (g_strcmp0(fu_firmware_get_id(FU_FIRMWARE(self)), "DG02") != 0)
		return TRUE;

	/* get hw_sku */
	st_inf = fu_struct_igsc_fwu_gws_image_info_parse_stream(stream, 0x0, error);
	if (st_inf == NULL)
		return FALSE;
	self->hw_sku = fu_struct_igsc_fwu_gws_image_info_get_instance_id(st_inf);
	return TRUE;
}

static gboolean
fu_igsc_code_firmware_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuIgscCodeFirmware *self = FU_IGSC_CODE_FIRMWARE(firmware);
	gsize streamsz = 0;
	g_autofree gchar *project = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) st_md1 = NULL;
	g_autoptr(GInputStream) stream_imgi = NULL;
	g_autoptr(GInputStream) stream_info = NULL;

	/* sanity check */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz > FU_IGSC_FIRMWARE_MAX_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "image size too big: 0x%x",
			    (guint)streamsz);
		return FALSE;
	}

	/* FuIfwiFptFirmware->parse */
	if (!FU_FIRMWARE_CLASS(fu_igsc_code_firmware_parent_class)
		 ->parse(firmware, stream, flags, error))
		return FALSE;

	stream_info = fu_firmware_get_image_by_idx_stream(FU_FIRMWARE(self),
							  FU_IFWI_FPT_FIRMWARE_IDX_INFO,
							  error);
	if (stream_info == NULL)
		return FALSE;

	/* check metadata header format */
	st_md1 = fu_struct_igsc_fwu_image_metadata_v1_parse_stream(stream_info, 0x0, error);
	if (st_md1 == NULL)
		return FALSE;
	if (fu_struct_igsc_fwu_image_metadata_v1_get_version_format(st_md1) != 0x01) {
		/* Note that it's still ok to use the V1 metadata struct to get the
		 * FW version because the FW version position and structure stays
		 * the same in all versions of the struct */
		g_warning("metadata format version is %u, instead of expected V1",
			  fu_struct_igsc_fwu_image_metadata_v1_get_version_format(st_md1));
	}
	project = fu_struct_igsc_fwu_image_metadata_v1_get_project(st_md1);
	fu_firmware_set_id(FU_FIRMWARE(self), project);
	version = g_strdup_printf("%04d.%04d",
				  fu_struct_igsc_fwu_image_metadata_v1_get_version_hotfix(st_md1),
				  fu_struct_igsc_fwu_image_metadata_v1_get_version_build(st_md1));
	fu_firmware_set_version(FU_FIRMWARE(self), version);

	/* get instance ID for image */
	stream_imgi = fu_firmware_get_image_by_idx_stream(FU_FIRMWARE(self),
							  FU_IFWI_FPT_FIRMWARE_IDX_IMGI,
							  error);
	if (stream_imgi == NULL)
		return FALSE;
	if (!fu_igsc_code_firmware_parse_imgi(self, stream_imgi, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_igsc_code_firmware_init(FuIgscCodeFirmware *self)
{
}

static void
fu_igsc_code_firmware_class_init(FuIgscCodeFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_igsc_code_firmware_parse;
	firmware_class->export = fu_igsc_code_firmware_export;
}

FuFirmware *
fu_igsc_code_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IGSC_CODE_FIRMWARE, NULL));
}
