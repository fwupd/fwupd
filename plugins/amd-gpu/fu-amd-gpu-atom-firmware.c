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
#include "fu-amd-gpu-atom-struct.h"

#define BIOS_VERSION_PREFIX "ATOMBIOSBK-AMD VER"
#define BIOS_STRING_LENGTH  43
#define STRLEN_NORMAL	    32
#define STRLEN_LONG	    64

struct _FuAmdGpuAtomFirmware {
	FuOpromFirmware parent_instance;
	gchar *part_number;
	gchar *asic;
	gchar *pci_type;
	gchar *memory_type;
	gchar *bios_date;
	gchar *model_name;
	gchar *config_filename;
};

/**
 * FuAmdGpuAtomFirmware:
 *
 * Firmware for AMD dGPUs.
 *
 * This parser collects information from the "CSM" image also known as
 * the ATOM image.
 *
 * This image contains strings that describe the version and the hardware
 * the ATOM is intended to be used for.
 */
G_DEFINE_TYPE(FuAmdGpuAtomFirmware, fu_amd_gpu_atom_firmware, FU_TYPE_OPROM_FIRMWARE)

static void
fu_amd_gpu_atom_firmware_export(FuFirmware *firmware,
				FuFirmwareExportFlags flags,
				XbBuilderNode *bn)
{
	FuAmdGpuAtomFirmware *self = FU_AMD_GPU_ATOM_FIRMWARE(firmware);

	FU_FIRMWARE_CLASS(fu_amd_gpu_atom_firmware_parent_class)->export(firmware, flags, bn);

	fu_xmlb_builder_insert_kv(bn, "part_number", self->part_number);
	fu_xmlb_builder_insert_kv(bn, "asic", self->asic);
	fu_xmlb_builder_insert_kv(bn, "pci_type", self->pci_type);
	fu_xmlb_builder_insert_kv(bn, "memory_type", self->memory_type);
	fu_xmlb_builder_insert_kv(bn, "bios_date", self->bios_date);
	fu_xmlb_builder_insert_kv(bn, "model_name", self->model_name);
	fu_xmlb_builder_insert_kv(bn, "config_filename", self->config_filename);
}

static gboolean
fu_amd_gpu_atom_firmware_validate(FuFirmware *firmware,
				  GInputStream *stream,
				  gsize offset,
				  GError **error)
{
	g_autoptr(GByteArray) atom = NULL;

	atom = fu_struct_atom_image_parse_stream(stream, offset, error);
	if (atom == NULL)
		return FALSE;
	return fu_struct_atom_rom21_header_validate_stream(stream,
							   fu_struct_atom_image_get_rom_loc(atom),
							   error);
}

const gchar *
fu_amd_gpu_atom_firmware_get_vbios_pn(FuFirmware *firmware)
{
	FuAmdGpuAtomFirmware *self = FU_AMD_GPU_ATOM_FIRMWARE(firmware);
	return self->part_number;
}

static gboolean
fu_amd_gpu_atom_firmware_parse_vbios_version(FuAmdGpuAtomFirmware *self,
					     const guint8 *buf,
					     gsize bufsz,
					     GError **error)
{
	gsize offset, base;
	g_autofree gchar *version = NULL;

	base = fu_firmware_get_addr(FU_FIRMWARE(self));
	if (!fu_memmem_safe(buf + base,
			    bufsz - base,
			    (const guint8 *)BIOS_VERSION_PREFIX,
			    sizeof(BIOS_VERSION_PREFIX) - 1,
			    &offset,
			    error)) {
		g_prefix_error(error, "failed to find anchor: ");
		return FALSE;
	}

	/* skip anchor */
	offset += base + sizeof(BIOS_VERSION_PREFIX) - 1;

	version = fu_memstrsafe(buf, bufsz, offset, BIOS_STRING_LENGTH, error);
	if (version == NULL)
		return FALSE;

	fu_firmware_set_version(FU_FIRMWARE(self), version);

	return TRUE;
}

static gboolean
fu_amd_gpu_atom_firmware_parse_vbios_date(FuAmdGpuAtomFirmware *self,
					  GByteArray *atom_image,
					  GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_atom_image_get_vbios_date(atom_image);

	if (st == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "ATOMBIOS date is invalid");
		return FALSE;
	}

	/* same date format as atom_get_vbios_date() */
	self->bios_date = g_strdup_printf("20%s/%s/%s %s:%s",
					  fu_struct_vbios_date_get_year(st),
					  fu_struct_vbios_date_get_month(st),
					  fu_struct_vbios_date_get_day(st),
					  fu_struct_vbios_date_get_hours(st),
					  fu_struct_vbios_date_get_minutes(st));

	return TRUE;
}

static gboolean
fu_amd_gpu_atom_firmware_parse_vbios_pn(FuAmdGpuAtomFirmware *self,
					const guint8 *buf,
					gsize bufsz,
					GByteArray *atom_image,
					GError **error)
{
	guint16 atombios_size;
	gint num_str, i;
	guint64 base;
	guint16 idx;
	g_autofree gchar *model = NULL;

	num_str = fu_struct_atom_image_get_num_strings(atom_image);
	if (num_str == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "ATOMBIOS number of strings is 0");
		return FALSE;
	}
	idx = fu_struct_atom_image_get_str_loc(atom_image);
	if (idx == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "ATOMBIOS string location is invalid");
		return FALSE;
	}

	/* make sure there is enough space for all the strings */
	atombios_size = fu_firmware_get_size(FU_FIRMWARE(self));
	if ((gsize)(idx + (num_str * (STRLEN_NORMAL - 1))) > atombios_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "bufsz is too small for all strings");
		return FALSE;
	}

	base = fu_firmware_get_addr(FU_FIRMWARE(self));

	/* parse atombios strings */
	for (i = 0; i < num_str; i++) {
		g_autofree char *str = NULL;

		str = fu_memstrsafe(buf, bufsz, base + idx, STRLEN_NORMAL - 1, error);
		if (str == NULL)
			return FALSE;

		idx += strlen(str) + 1;

		switch (i) {
		case FU_ATOM_STRING_INDEX_PART_NUMBER:
			self->part_number = g_steal_pointer(&str);
			break;
		case FU_ATOM_STRING_INDEX_ASIC:
			self->asic = g_steal_pointer(&str);
			break;
		case FU_ATOM_STRING_INDEX_PCI_TYPE:
			self->pci_type = g_steal_pointer(&str);
			break;
		case FU_ATOM_STRING_INDEX_MEMORY_TYPE:
			self->memory_type = g_steal_pointer(&str);
			break;
		default:
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unknown string index: %d",
				    i);
			return FALSE;
		}
	}

	/* skip the following 2 chars: 0x0D 0x0A */
	idx += 2;

	/* make sure there is enough space for name string */
	if ((gsize)(idx + STRLEN_LONG - 1) > atombios_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "bufsz is too small for name string");
		return FALSE;
	}

	model = fu_memstrsafe(buf, bufsz, base + idx, STRLEN_LONG - 1, error);
	if (model == NULL)
		return FALSE;
	self->model_name = fu_strstrip(model);

	return TRUE;
}

static gboolean
fu_amd_gpu_atom_firmware_parse_config_filename(FuAmdGpuAtomFirmware *self,
					       const guint8 *buf,
					       gsize bufsz,
					       GByteArray *atom_header,
					       GError **error)
{
	g_autofree gchar *config_filename = NULL;

	config_filename =
	    fu_memstrsafe(buf,
			  bufsz,
			  fu_firmware_get_addr(FU_FIRMWARE(self)) +
			      fu_struct_atom_rom21_header_get_config_filename_offset(atom_header),
			  STRLEN_LONG - 1,
			  error);
	if (config_filename == NULL)
		return FALSE;

	/* this function is called twice, but value only stored once */
	if (self->config_filename == NULL)
		self->config_filename = fu_strstrip(config_filename);

	return TRUE;
}

static gboolean
fu_amd_gpu_atom_firmware_parse(FuFirmware *firmware,
			       GInputStream *stream,
			       gsize offset,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuAmdGpuAtomFirmware *self = FU_AMD_GPU_ATOM_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint32 loc;
	const guint8 *buf;
	g_autoptr(GByteArray) atom_image = NULL;
	g_autoptr(GByteArray) atom_rom = NULL;
	g_autoptr(GBytes) fw = NULL;

	if (!FU_FIRMWARE_CLASS(fu_amd_gpu_atom_firmware_parent_class)
		 ->parse(firmware, stream, offset, flags, error))
		return FALSE;

	/* atom rom image */
	atom_image = fu_struct_atom_image_parse_stream(stream, offset, error);
	if (atom_image == NULL)
		return FALSE;

	/* unit is 512 bytes */
	fu_firmware_set_size(firmware, fu_struct_atom_image_get_size(atom_image) * 512);

	/* atom rom header */
	loc = fu_struct_atom_image_get_rom_loc(atom_image) + offset;
	atom_rom = fu_struct_atom_rom21_header_parse_stream(stream, loc, error);
	if (atom_rom == NULL)
		return FALSE;

	fw = fu_input_stream_read_bytes(stream, offset, G_MAXSIZE, error);
	if (fw == NULL)
		return FALSE;
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_amd_gpu_atom_firmware_parse_config_filename(self, buf, bufsz, atom_rom, error))
		return FALSE;
	if (!fu_amd_gpu_atom_firmware_parse_vbios_date(self, atom_image, error))
		return FALSE;
	if (!fu_amd_gpu_atom_firmware_parse_vbios_pn(self, buf, bufsz, atom_image, error))
		return FALSE;
	if (!fu_amd_gpu_atom_firmware_parse_vbios_version(self, buf, bufsz, error))
		return FALSE;

	return TRUE;
}

static void
fu_amd_gpu_atom_firmware_init(FuAmdGpuAtomFirmware *self)
{
}

static void
fu_amd_gpu_atom_firmware_finalize(GObject *object)
{
	FuAmdGpuAtomFirmware *self = FU_AMD_GPU_ATOM_FIRMWARE(object);
	g_free(self->part_number);
	g_free(self->asic);
	g_free(self->pci_type);
	g_free(self->memory_type);
	g_free(self->bios_date);
	g_free(self->model_name);
	g_free(self->config_filename);
	G_OBJECT_CLASS(fu_amd_gpu_atom_firmware_parent_class)->finalize(object);
}

static void
fu_amd_gpu_atom_firmware_class_init(FuAmdGpuAtomFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_amd_gpu_atom_firmware_finalize;
	firmware_class->parse = fu_amd_gpu_atom_firmware_parse;
	firmware_class->export = fu_amd_gpu_atom_firmware_export;
	firmware_class->validate = fu_amd_gpu_atom_firmware_validate;
}

FuFirmware *
fu_amd_gpu_atom_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_AMD_GPU_ATOM_FIRMWARE, NULL));
}
