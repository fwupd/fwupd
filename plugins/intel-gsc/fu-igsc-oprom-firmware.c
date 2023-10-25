/*
 * Copyright (C) 2022 Intel
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-igsc-oprom-firmware.h"
#include "fu-igsc-struct.h"

struct _FuIgscOpromFirmware {
	FuOpromFirmware parent_instance;
	guint16 major_version;
	GPtrArray *device_infos; /* of FuIgscFwdataDeviceInfo */
};

typedef struct {
	guint16 vendor_id; /* may be 0x0 */
	guint16 device_id; /* may be 0x0 */
	guint16 subsys_vendor_id;
	guint16 subsys_device_id;
} FuIgscFwdataDeviceInfo;

G_DEFINE_TYPE(FuIgscOpromFirmware, fu_igsc_oprom_firmware, FU_TYPE_OPROM_FIRMWARE)

static void
fu_igsc_oprom_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIgscOpromFirmware *self = FU_IGSC_OPROM_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "major_version", self->major_version);
	fu_xmlb_builder_insert_kx(bn, "device_infos", self->device_infos->len);
}

guint16
fu_igsc_oprom_firmware_get_major_version(FuIgscOpromFirmware *self)
{
	g_return_val_if_fail(FU_IS_IGSC_OPROM_FIRMWARE(self), G_MAXUINT16);
	return self->major_version;
}

gboolean
fu_igsc_oprom_firmware_has_allowlist(FuIgscOpromFirmware *self)
{
	g_return_val_if_fail(FU_IS_IGSC_OPROM_FIRMWARE(self), FALSE);
	return self->device_infos->len > 0;
}

gboolean
fu_igsc_oprom_firmware_match_device(FuIgscOpromFirmware *self,
				    guint16 vendor_id,
				    guint16 device_id,
				    guint16 subsys_vendor_id,
				    guint16 subsys_device_id,
				    GError **error)
{
	g_return_val_if_fail(FU_IS_IGSC_OPROM_FIRMWARE(self), FALSE);

	for (guint i = 0; i < self->device_infos->len; i++) {
		FuIgscFwdataDeviceInfo *info = g_ptr_array_index(self->device_infos, i);
		if (info->vendor_id == 0x0 && info->device_id == 0x0 &&
		    info->subsys_vendor_id == subsys_vendor_id &&
		    info->subsys_device_id == subsys_device_id)
			return TRUE;
		if (info->vendor_id == vendor_id && info->device_id == device_id &&
		    info->subsys_vendor_id == subsys_vendor_id &&
		    info->subsys_device_id == subsys_device_id)
			return TRUE;
	}

	/* not us */
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_NOT_FOUND,
		    "could not find 0x%04x:0x%04x 0x%04x:0x%04x in the image",
		    vendor_id,
		    device_id,
		    subsys_vendor_id,
		    subsys_device_id);
	return FALSE;
}

/* extension types */
#define MFT_EXT_TYPE_DEVICE_TYPE	 7
#define MDF_EXT_TYPE_MODULE_ATTR	 10
#define MFT_EXT_TYPE_SIGNED_PACKAGE_INFO 15
#define MFT_EXT_TYPE_IFWI_PART_MAN	 22
#define MFT_EXT_TYPE_DEVICE_ID_ARRAY	 55

static gboolean
fu_igsc_oprom_firmware_parse_extension(FuIgscOpromFirmware *self, FuFirmware *fw, GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	/* get data */
	blob = fu_firmware_get_bytes(fw, error);
	if (blob == NULL)
		return FALSE;
	if (fu_firmware_get_idx(fw) == MFT_EXT_TYPE_DEVICE_TYPE) {
		for (gsize offset = 0; offset < g_bytes_get_size(blob);
		     offset += FU_STRUCT_IGSC_OPROM_SUBSYSTEM_DEVICE_ID_SIZE) {
			g_autofree FuIgscFwdataDeviceInfo *info = g_new0(FuIgscFwdataDeviceInfo, 1);
			g_autoptr(GByteArray) st = NULL;
			st = fu_struct_igsc_oprom_subsystem_device_id_parse_bytes(blob,
										  offset,
										  error);
			if (st == NULL)
				return FALSE;
			info->subsys_vendor_id =
			    fu_struct_igsc_oprom_subsystem_device_id_get_subsys_vendor_id(st);
			info->subsys_device_id =
			    fu_struct_igsc_oprom_subsystem_device_id_get_subsys_device_id(st);
			g_ptr_array_add(self->device_infos, g_steal_pointer(&info));
		}
	} else if (fu_firmware_get_idx(fw) == MFT_EXT_TYPE_DEVICE_ID_ARRAY) {
		for (gsize offset = 0; offset < g_bytes_get_size(blob);
		     offset += FU_STRUCT_IGSC_OPROM_SUBSYSTEM_DEVICE4_ID_SIZE) {
			g_autofree FuIgscFwdataDeviceInfo *info = g_new0(FuIgscFwdataDeviceInfo, 1);
			g_autoptr(GByteArray) st = NULL;
			st = fu_struct_igsc_oprom_subsystem_device4_id_parse_bytes(blob,
										   offset,
										   error);
			if (st == NULL)
				return FALSE;
			info->vendor_id =
			    fu_struct_igsc_oprom_subsystem_device4_id_get_vendor_id(st);
			info->device_id =
			    fu_struct_igsc_oprom_subsystem_device4_id_get_device_id(st);
			info->subsys_vendor_id =
			    fu_struct_igsc_oprom_subsystem_device4_id_get_subsys_vendor_id(st);
			info->subsys_device_id =
			    fu_struct_igsc_oprom_subsystem_device4_id_get_subsys_device_id(st);
			g_ptr_array_add(self->device_infos, g_steal_pointer(&info));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_igsc_oprom_firmware_parse(FuFirmware *firmware,
			     GBytes *fw,
			     gsize offset,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuIgscOpromFirmware *self = FU_IGSC_OPROM_FIRMWARE(firmware);
	g_autoptr(FuFirmware) fw_cpd = NULL;
	g_autoptr(GPtrArray) cpd_imgs = NULL;

	/* FuOpromFirmware->parse */
	if (!FU_FIRMWARE_CLASS(fu_igsc_oprom_firmware_parent_class)
		 ->parse(firmware, fw, offset, flags, error))
		return FALSE;

	/* check sanity */
	if (fu_oprom_firmware_get_subsystem(FU_OPROM_FIRMWARE(firmware)) !=
	    FU_OPROM_FIRMWARE_SUBSYSTEM_EFI_BOOT_SRV_DRV) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid subsystem, got 0x%x, expected 0x%x",
			    fu_oprom_firmware_get_subsystem(FU_OPROM_FIRMWARE(firmware)),
			    (guint)FU_OPROM_FIRMWARE_SUBSYSTEM_EFI_BOOT_SRV_DRV);
		return FALSE;
	}
	if (fu_oprom_firmware_get_machine_type(FU_OPROM_FIRMWARE(firmware)) !=
	    FU_OPROM_FIRMWARE_MACHINE_TYPE_X64) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid machine type, got 0x%x, expected 0x%x",
			    fu_oprom_firmware_get_machine_type(FU_OPROM_FIRMWARE(firmware)),
			    (guint)FU_OPROM_FIRMWARE_MACHINE_TYPE_X64);
		return FALSE;
	}
	if (fu_oprom_firmware_get_compression_type(FU_OPROM_FIRMWARE(firmware)) !=
	    FU_OPROM_FIRMWARE_COMPRESSION_TYPE_NONE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid compression type, got 0x%x, expected 0x%x (uncompressed)",
			    fu_oprom_firmware_get_compression_type(FU_OPROM_FIRMWARE(firmware)),
			    (guint)FU_OPROM_FIRMWARE_COMPRESSION_TYPE_NONE);
		return FALSE;
	}

	/* get CPD */
	fw_cpd = fu_firmware_get_image_by_id(firmware, "cpd", error);
	if (fw_cpd == NULL)
		return FALSE;
	if (!FU_IS_IFWI_CPD_FIRMWARE(fw_cpd)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "CPD was not FuIfwiCpdFirmware");
		return FALSE;
	}

	/* parse all the manifest extensions */
	cpd_imgs = fu_firmware_get_images(fw_cpd);
	for (guint i = 0; i < cpd_imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(cpd_imgs, i);
		if (!fu_igsc_oprom_firmware_parse_extension(self, img, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_igsc_oprom_firmware_init(FuIgscOpromFirmware *self)
{
	self->device_infos = g_ptr_array_new_with_free_func(g_free);
}

static void
fu_igsc_oprom_firmware_finalize(GObject *object)
{
	FuIgscOpromFirmware *self = FU_IGSC_OPROM_FIRMWARE(object);

	g_ptr_array_unref(self->device_infos);

	G_OBJECT_CLASS(fu_igsc_oprom_firmware_parent_class)->finalize(object);
}

static void
fu_igsc_oprom_firmware_class_init(FuIgscOpromFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_igsc_oprom_firmware_finalize;
	klass_firmware->parse = fu_igsc_oprom_firmware_parse;
	klass_firmware->export = fu_igsc_oprom_firmware_export;
}
