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
	g_autoptr(FuStructEfs) st = NULL;
	st = fu_struct_efs_parse_stream(stream, 0, error);
	if (st == NULL)
		return FALSE;
	return fu_struct_psp_dir_validate_stream(stream, fu_struct_efs_get_psp_dir_loc(st), error);
}

static gboolean
fu_amd_gpu_psp_firmware_parse_l2(FuAmdGpuPspFirmware *self,
				 GInputStream *stream,
				 gsize offset,
				 GError **error)
{
	g_autoptr(FuStructPspDir) st_dir = NULL;

	/* parse the L2 entries */
	st_dir = fu_struct_psp_dir_parse_stream(stream, offset, error);
	if (st_dir == NULL)
		return FALSE;
	offset += st_dir->buf->len;
	for (guint i = 0; i < fu_struct_psp_dir_get_total_entries(st_dir); i++) {
		g_autoptr(FuStructPspDirTable) st_entry = NULL;
		st_entry = fu_struct_psp_dir_table_parse_stream(stream, offset, error);
		if (st_entry == NULL)
			return FALSE;
		offset += st_entry->buf->len;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_amd_gpu_psp_firmware_parse_l1(FuAmdGpuPspFirmware *self,
				 GInputStream *stream,
				 gsize offset,
				 FuFirmwareParseFlags flags,
				 GError **error)
{
	g_autoptr(FuStructPspDir) st_dir = NULL;

	/* parse the L1 entries */
	st_dir = fu_struct_psp_dir_parse_stream(stream, offset, error);
	if (st_dir == NULL)
		return FALSE;
	offset += st_dir->buf->len;
	for (guint i = 0; i < fu_struct_psp_dir_get_total_entries(st_dir); i++) {
		guint loc;
		guint sz;
		g_autoptr(FuStructPspDirTable) st_entry = NULL;
		g_autoptr(FuStructImageSlotHeader) st_hdr = NULL;
		g_autoptr(FuFirmware) ish_img = fu_firmware_new();
		g_autoptr(FuFirmware) csm_img = fu_amd_gpu_atom_firmware_new();
		g_autoptr(FuFirmware) l2_img = fu_firmware_new();
		g_autoptr(GInputStream) l2_stream = NULL;

		st_entry = fu_struct_psp_dir_table_parse_stream(stream, offset, error);
		if (st_entry == NULL)
			return FALSE;

		switch (fu_struct_psp_dir_table_get_fw_id(st_entry)) {
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
				    fu_struct_psp_dir_table_get_fw_id(st_entry));
			return FALSE;
		}

		/* parse the image slot header */
		loc = fu_struct_psp_dir_table_get_loc(st_entry);
		st_hdr = fu_struct_image_slot_header_parse_stream(stream, loc, error);
		if (st_hdr == NULL)
			return FALSE;
		if (!fu_firmware_parse_stream(ish_img, stream, loc, flags, error))
			return FALSE;
		fu_firmware_set_addr(ish_img, loc);
		if (!fu_firmware_add_image(FU_FIRMWARE(self), ish_img, error))
			return FALSE;

		/* parse the csm image */
		loc = fu_struct_image_slot_header_get_loc_csm(st_hdr);
		fu_firmware_set_addr(csm_img, loc);
		if (!fu_firmware_parse_stream(csm_img, stream, loc, flags, error))
			return FALSE;

		loc = fu_struct_image_slot_header_get_loc(st_hdr);
		sz = fu_struct_image_slot_header_get_slot_max_size(st_hdr);
		switch (fu_struct_image_slot_header_get_fw_id(st_hdr)) {
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
				    fu_struct_image_slot_header_get_fw_id(st_hdr));
			return FALSE;
		}
		if (!fu_firmware_add_image(l2_img, csm_img, error))
			return FALSE;

		l2_stream = fu_partial_input_stream_new(stream, loc, sz, error);
		if (l2_stream == NULL)
			return FALSE;
		fu_firmware_set_addr(l2_img, loc);
		if (!fu_firmware_parse_stream(l2_img, l2_stream, 0x0, flags, error))
			return FALSE;
		if (!fu_firmware_add_image(ish_img, l2_img, error))
			return FALSE;

		/* parse the partition */
		if (!fu_amd_gpu_psp_firmware_parse_l2(self, stream, loc, error))
			return FALSE;

		/* next entry */
		offset += st_entry->buf->len;
	}

	return TRUE;
}

static gboolean
fu_amd_gpu_psp_firmware_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      FuFirmwareParseFlags flags,
			      GError **error)
{
	FuAmdGpuPspFirmware *self = FU_AMD_GPU_PSP_FIRMWARE(firmware);
	g_autoptr(FuStructEfs) st = NULL;

	st = fu_struct_efs_parse_stream(stream, 0, error);
	if (st == NULL)
		return FALSE;
	self->dir_location = fu_struct_efs_get_psp_dir_loc(st);

	return fu_amd_gpu_psp_firmware_parse_l1(self, stream, self->dir_location, flags, error);
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
