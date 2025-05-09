/*
 * Copyright 2022 Intel
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-igsc-oprom-firmware.h"
#include "fu-igsc-struct.h"

struct _FuIgscOpromFirmware {
	FuOpromFirmware parent_instance;
	guint16 major_version;
	GPtrArray *device_infos; /* of FuIgscFwdataDeviceInfo4 */
};

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
		FuIgscFwdataDeviceInfo4 *info = g_ptr_array_index(self->device_infos, i);
		if (fu_igsc_fwdata_device_info4_get_vendor_id(info) == 0x0 &&
		    fu_igsc_fwdata_device_info4_get_device_id(info) == 0x0 &&
		    fu_igsc_fwdata_device_info4_get_subsys_vendor_id(info) == subsys_vendor_id &&
		    fu_igsc_fwdata_device_info4_get_subsys_device_id(info) == subsys_device_id)
			return TRUE;
		if (fu_igsc_fwdata_device_info4_get_vendor_id(info) == vendor_id &&
		    fu_igsc_fwdata_device_info4_get_device_id(info) == device_id &&
		    fu_igsc_fwdata_device_info4_get_subsys_vendor_id(info) == subsys_vendor_id &&
		    fu_igsc_fwdata_device_info4_get_subsys_device_id(info) == subsys_device_id)
			return TRUE;
	}

	/* not us */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "could not find 0x%04x:0x%04x 0x%04x:0x%04x in the image",
		    vendor_id,
		    device_id,
		    subsys_vendor_id,
		    subsys_device_id);
	return FALSE;
}

static gboolean
fu_igsc_oprom_firmware_parse_extension(FuIgscOpromFirmware *self, FuFirmware *fw, GError **error)
{
	gsize streamsz = 0;
	g_autoptr(GInputStream) stream = NULL;

	/* get data */
	stream = fu_firmware_get_stream(fw, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (fu_firmware_get_idx(fw) == FU_IGSC_FWU_EXT_TYPE_DEVICE_TYPE) {
		for (gsize offset = 0; offset < streamsz;
		     offset += FU_IGSC_FWDATA_DEVICE_INFO2_SIZE) {
			g_autoptr(FuIgscFwdataDeviceInfo2) st = NULL;
			g_autoptr(FuIgscFwdataDeviceInfo4) st4 = fu_igsc_fwdata_device_info4_new();
			st = fu_igsc_fwdata_device_info2_parse_stream(stream, offset, error);
			if (st == NULL)
				return FALSE;
			fu_igsc_fwdata_device_info4_set_vendor_id(st4, 0x0);
			fu_igsc_fwdata_device_info4_set_device_id(st4, 0x0);
			fu_igsc_fwdata_device_info4_set_subsys_vendor_id(
			    st4,
			    fu_igsc_fwdata_device_info2_get_subsys_vendor_id(st));
			fu_igsc_fwdata_device_info4_set_subsys_device_id(
			    st4,
			    fu_igsc_fwdata_device_info2_get_subsys_device_id(st));
			g_ptr_array_add(self->device_infos, g_steal_pointer(&st4));
		}
	} else if (fu_firmware_get_idx(fw) == FU_IGSC_FWU_EXT_TYPE_DEVICE_ID_ARRAY) {
		for (gsize offset = 0; offset < streamsz;
		     offset += FU_IGSC_FWDATA_DEVICE_INFO4_SIZE) {
			g_autoptr(FuIgscFwdataDeviceInfo4) st = NULL;
			st = fu_igsc_fwdata_device_info4_parse_stream(stream, offset, error);
			if (st == NULL)
				return FALSE;
			g_ptr_array_add(self->device_infos, g_steal_pointer(&st));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_igsc_oprom_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	FuIgscOpromFirmware *self = FU_IGSC_OPROM_FIRMWARE(firmware);
	g_autoptr(FuFirmware) fw_cpd = NULL;
	g_autoptr(GPtrArray) cpd_imgs = NULL;

	/* FuOpromFirmware->parse */
	if (!FU_FIRMWARE_CLASS(fu_igsc_oprom_firmware_parent_class)
		 ->parse(firmware, stream, flags, error))
		return FALSE;

	/* check sanity */
	if (fu_oprom_firmware_get_subsystem(FU_OPROM_FIRMWARE(firmware)) !=
	    FU_OPROM_FIRMWARE_SUBSYSTEM_EFI_BOOT_SRV_DRV) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid subsystem, got 0x%x, expected 0x%x",
			    fu_oprom_firmware_get_subsystem(FU_OPROM_FIRMWARE(firmware)),
			    (guint)FU_OPROM_FIRMWARE_SUBSYSTEM_EFI_BOOT_SRV_DRV);
		return FALSE;
	}
	if (fu_oprom_firmware_get_machine_type(FU_OPROM_FIRMWARE(firmware)) !=
	    FU_OPROM_FIRMWARE_MACHINE_TYPE_X64) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid machine type, got 0x%x, expected 0x%x",
			    fu_oprom_firmware_get_machine_type(FU_OPROM_FIRMWARE(firmware)),
			    (guint)FU_OPROM_FIRMWARE_MACHINE_TYPE_X64);
		return FALSE;
	}
	if (fu_oprom_firmware_get_compression_type(FU_OPROM_FIRMWARE(firmware)) !=
	    FU_OPROM_FIRMWARE_COMPRESSION_TYPE_NONE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
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
	self->device_infos =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_igsc_fwdata_device_info4_unref);
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
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_igsc_oprom_firmware_finalize;
	firmware_class->parse = fu_igsc_oprom_firmware_parse;
	firmware_class->export = fu_igsc_oprom_firmware_export;
}
