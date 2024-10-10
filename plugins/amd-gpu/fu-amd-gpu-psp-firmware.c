/*
 * Copyright 2023 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-gpu-atom-firmware.h"
#include "fu-amd-gpu-psp-firmware.h"
#include "fu-amd-gpu-psp-struct.h"

struct _FuAmdGpuPspFirmware {
	FuFirmware parent_instance;
	guint32 dir_location;
};
/**
 * FuAmdGpuPspFirmware:
 *
 * An AMD PSP firmware image
 *
 * The firmware is structured in an Embedded Firmware Structure (EFS).
 * Within the EFS is a point to an "L1 PSP directory table".
 *
 * The L1 PSP directory table contains entries which point to
 * "Image Slot Headers" (ISH).
 *
 * The ISH headers contain entries that point to a given partition (A or B).
 * The partition contains an "L2 PSP directory table".
 *
 * The L2 directory table specifies a variety of IDs.  Supported IDs will
 * be parsed by other firmware parsers.
 */

G_DEFINE_TYPE(FuAmdGpuPspFirmware, fu_amd_gpu_psp_firmware, FU_TYPE_FIRMWARE)

static void
fu_amd_gpu_psp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuAmdGpuPspFirmware *self = FU_AMD_GPU_PSP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "dir_location", self->dir_location);
}

static gboolean
fu_amd_gpu_psp_firmware_validate(FuFirmware *firmware,
				 GInputStream *stream,
				 gsize offset,
				 GError **error)
{
	g_autoptr(GByteArray) efs = NULL;

	efs = fu_struct_efs_parse_stream(stream, 0, error);
	if (efs == NULL)
		return FALSE;
	return fu_struct_psp_dir_validate_stream(stream, fu_struct_efs_get_psp_dir_loc(efs), error);
}

static gboolean
fu_amd_gpu_psp_firmware_parse_l2(FuFirmware *firmware,
				 GInputStream *stream,
				 gsize offset,
				 GError **error)
{
	g_autoptr(FuStructPspDirTable) l2 = NULL;

	/* parse the L2 entries */
	l2 = fu_struct_psp_dir_table_parse_stream(stream, offset, error);
	if (l2 == NULL)
		return FALSE;
	offset += l2->len;
	for (guint i = 0; i < fu_struct_psp_dir_get_total_entries(l2); i++) {
		g_autoptr(FuStructPspDirTable) l2_entry = NULL;
		l2_entry = fu_struct_psp_dir_table_parse_stream(stream, offset, error);
		if (l2_entry == NULL)
			return FALSE;
		offset += l2_entry->len;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_amd_gpu_psp_firmware_parse_l1(FuFirmware *firmware,
				 GInputStream *stream,
				 gsize offset,
				 GError **error)
{
	g_autoptr(FuStructPspDir) l1 = NULL;

	/* parse the L1 entries */
	l1 = fu_struct_psp_dir_parse_stream(stream, offset, error);
	if (l1 == NULL)
		return FALSE;
	offset += l1->len;
	for (guint i = 0; i < fu_struct_psp_dir_get_total_entries(l1); i++) {
		guint loc;
		guint sz;
		g_autoptr(FuStructPspDirTable) l1_entry = NULL;
		g_autoptr(FuStructImageSlotHeader) ish = NULL;
		g_autoptr(FuFirmware) ish_img = fu_firmware_new();
		g_autoptr(FuFirmware) csm_img = fu_amd_gpu_atom_firmware_new();
		g_autoptr(FuFirmware) l2_img = fu_firmware_new();
		g_autoptr(GInputStream) l2_stream = NULL;

		l1_entry = fu_struct_psp_dir_table_parse_stream(stream, offset, error);
		if (l1_entry == NULL)
			return FALSE;

		switch (fu_struct_psp_dir_table_get_fw_id(l1_entry)) {
		case FU_FWID_ISH_A:
			fu_firmware_set_id(ish_img, "ISH_A");
			break;
		case FU_FWID_ISH_B:
			fu_firmware_set_id(ish_img, "ISH_B");
			break;
		default:
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Unknown ISH FWID: %x",
				    fu_struct_psp_dir_table_get_fw_id(l1_entry));
			return FALSE;
		}

		/* parse the image slot header */
		loc = fu_struct_psp_dir_table_get_loc(l1_entry);
		ish = fu_struct_image_slot_header_parse_stream(stream, loc, error);
		if (ish == NULL)
			return FALSE;
		if (!fu_firmware_parse_stream(ish_img, stream, loc, FWUPD_INSTALL_FLAG_NONE, error))
			return FALSE;
		fu_firmware_set_addr(ish_img, loc);
		fu_firmware_add_image(firmware, ish_img);

		/* parse the csm image */
		loc = fu_struct_image_slot_header_get_loc_csm(ish);
		fu_firmware_set_addr(csm_img, loc);
		if (!fu_firmware_parse_stream(csm_img, stream, loc, FWUPD_INSTALL_FLAG_NONE, error))
			return FALSE;

		loc = fu_struct_image_slot_header_get_loc(ish);
		sz = fu_struct_image_slot_header_get_slot_max_size(ish);
		switch (fu_struct_image_slot_header_get_fw_id(ish)) {
		case FU_FWID_PARTITION_A_L2:
			fu_firmware_set_id(l2_img, "PARTITION_A");
			fu_firmware_set_id(csm_img, "ATOM_CSM_A");
			break;
		case FU_FWID_PARTITION_B_L2:
			fu_firmware_set_id(l2_img, "PARTITION_B");
			fu_firmware_set_id(csm_img, "ATOM_CSM_B");
			break;
		default:
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unknown Partition FWID: %x",
				    fu_struct_image_slot_header_get_fw_id(ish));
			return FALSE;
		}
		fu_firmware_add_image(l2_img, csm_img);

		l2_stream = fu_partial_input_stream_new(stream, loc, sz, error);
		if (l2_stream == NULL)
			return FALSE;
		fu_firmware_set_addr(l2_img, loc);
		if (!fu_firmware_parse_stream(l2_img,
					      l2_stream,
					      0x0,
					      FWUPD_INSTALL_FLAG_NONE,
					      error))
			return FALSE;
		fu_firmware_add_image(ish_img, l2_img);

		/* parse the partition */
		if (!fu_amd_gpu_psp_firmware_parse_l2(l2_img, stream, loc, error))
			return FALSE;

		/* next entry */
		offset += l1_entry->len;
	}

	return TRUE;
}

static gboolean
fu_amd_gpu_psp_firmware_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuAmdGpuPspFirmware *self = FU_AMD_GPU_PSP_FIRMWARE(firmware);
	g_autoptr(GByteArray) efs = NULL;

	efs = fu_struct_efs_parse_stream(stream, 0, error);
	if (efs == NULL)
		return FALSE;
	self->dir_location = fu_struct_efs_get_psp_dir_loc(efs);

	return fu_amd_gpu_psp_firmware_parse_l1(firmware, stream, self->dir_location, error);
}

static void
fu_amd_gpu_psp_firmware_init(FuAmdGpuPspFirmware *self)
{
}

static void
fu_amd_gpu_psp_firmware_class_init(FuAmdGpuPspFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_amd_gpu_psp_firmware_validate;
	firmware_class->parse = fu_amd_gpu_psp_firmware_parse;
	firmware_class->export = fu_amd_gpu_psp_firmware_export;
}

/**
 * fu_amd_gpu_psp_firmware_new
 *
 * Creates a new #FuFirmware of sub type amd-gpu-psp
 *
 * Since: 1.9.6
 **/
FuFirmware *
fu_amd_gpu_psp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_AMD_GPU_PSP_FIRMWARE, NULL));
}
