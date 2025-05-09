/*
 * Copyright 2022 Intel
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-igsc-aux-firmware.h"
#include "fu-igsc-heci.h"
#include "fu-igsc-struct.h"

struct _FuIgscAuxFirmware {
	FuIfwiFptFirmware parent_instance;
	guint32 oem_version;
	guint16 major_version;
	guint16 major_vcn;
	GPtrArray *device_infos; /* of FuIgscFwdataDeviceInfo4 */
	gboolean has_manifest_ext;
};

G_DEFINE_TYPE(FuIgscAuxFirmware, fu_igsc_aux_firmware, FU_TYPE_IFWI_FPT_FIRMWARE)

static void
fu_igsc_aux_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIgscAuxFirmware *self = FU_IGSC_AUX_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "oem_version", self->oem_version);
	fu_xmlb_builder_insert_kx(bn, "major_version", self->major_version);
	fu_xmlb_builder_insert_kx(bn, "major_vcn", self->major_vcn);
	fu_xmlb_builder_insert_kx(bn, "device_infos", self->device_infos->len);
	fu_xmlb_builder_insert_kb(bn, "has_manifest_ext", self->has_manifest_ext);
}

gboolean
fu_igsc_aux_firmware_match_device(FuIgscAuxFirmware *self,
				  guint16 vendor_id,
				  guint16 device_id,
				  guint16 subsys_vendor_id,
				  guint16 subsys_device_id,
				  GError **error)
{
	g_return_val_if_fail(FU_IS_IGSC_AUX_FIRMWARE(self), FALSE);

	for (guint i = 0; i < self->device_infos->len; i++) {
		FuIgscFwdataDeviceInfo4 *info = g_ptr_array_index(self->device_infos, i);
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

guint32
fu_igsc_aux_firmware_get_oem_version(FuIgscAuxFirmware *self)
{
	g_return_val_if_fail(FU_IS_IGSC_AUX_FIRMWARE(self), G_MAXUINT32);
	return self->oem_version;
}

guint16
fu_igsc_aux_firmware_get_major_version(FuIgscAuxFirmware *self)
{
	g_return_val_if_fail(FU_IS_IGSC_AUX_FIRMWARE(self), G_MAXUINT16);
	return self->major_version;
}

guint16
fu_igsc_aux_firmware_get_major_vcn(FuIgscAuxFirmware *self)
{
	g_return_val_if_fail(FU_IS_IGSC_AUX_FIRMWARE(self), G_MAXUINT16);
	return self->major_vcn;
}

static gboolean
fu_igsc_aux_firmware_parse_version(FuIgscAuxFirmware *self, GError **error)
{
	g_autoptr(FuStructIgscFwdataVersion) st = NULL;
	g_autoptr(GInputStream) stream = NULL;

	stream = fu_firmware_get_image_by_idx_stream(FU_FIRMWARE(self),
						     FU_IFWI_FPT_FIRMWARE_IDX_SDTA,
						     error);
	if (stream == NULL)
		return FALSE;
	st = fu_struct_igsc_fwdata_version_parse_stream(stream,
							FU_STRUCT_IGSC_FWU_HECI_IMAGE_METADATA_SIZE,
							error);
	if (st == NULL)
		return FALSE;
	self->oem_version = fu_struct_igsc_fwdata_version_get_oem_manuf_data_version(st);
	self->major_vcn = fu_struct_igsc_fwdata_version_get_major_vcn(st);
	self->major_version = fu_struct_igsc_fwdata_version_get_major_version(st);
	return TRUE;
}

static gboolean
fu_igsc_aux_firmware_parse_extension(FuIgscAuxFirmware *self, FuFirmware *fw, GError **error)
{
	g_autoptr(GInputStream) stream = NULL;

	/* get data */
	stream = fu_firmware_get_stream(fw, error);
	if (stream == NULL)
		return FALSE;

	if (fu_firmware_get_idx(fw) == FU_IGSC_FWU_EXT_TYPE_DEVICE_IDS) {
		for (gsize offset = 0; offset < fu_firmware_get_size(fw);
		     offset += FU_IGSC_FWDATA_DEVICE_INFO4_SIZE) {
			g_autoptr(FuIgscFwdataDeviceInfo4) st = NULL;
			st = fu_igsc_fwdata_device_info4_parse_stream(stream, offset, error);
			if (st == NULL)
				return FALSE;
			g_ptr_array_add(self->device_infos, g_steal_pointer(&st));
		}
	} else if (fu_firmware_get_idx(fw) == FU_IGSC_FWU_EXT_TYPE_FWDATA_UPDATE) {
		if (fu_firmware_get_size(fw) != FU_STRUCT_IGSC_FWDATA_UPDATE_EXT_SIZE) {
			g_set_error(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "signed data update manifest ext was 0x%x bytes and expected 0x%x",
			    (guint)fu_firmware_get_size(fw),
			    (guint)FU_STRUCT_IGSC_FWDATA_UPDATE_EXT_SIZE);
			return FALSE;
		}
		self->has_manifest_ext = TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_igsc_aux_firmware_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   FuFirmwareParseFlags flags,
			   GError **error)
{
	FuIgscAuxFirmware *self = FU_IGSC_AUX_FIRMWARE(firmware);
	g_autoptr(FuFirmware) fw_cpd = fu_ifwi_cpd_firmware_new();
	g_autoptr(FuFirmware) fw_manifest = NULL;
	g_autoptr(GInputStream) stream_dataimg = NULL;
	g_autoptr(GPtrArray) imgs = NULL;

	/* FuIfwiFptFirmware->parse */
	if (!FU_FIRMWARE_CLASS(fu_igsc_aux_firmware_parent_class)
		 ->parse(firmware, stream, flags, error))
		return FALSE;

	/* parse data section */
	stream_dataimg =
	    fu_firmware_get_image_by_idx_stream(firmware, FU_IFWI_FPT_FIRMWARE_IDX_SDTA, error);
	if (stream_dataimg == NULL)
		return FALSE;

	/* parse as CPD */
	if (!fu_firmware_parse_stream(fw_cpd, stream_dataimg, 0x0, flags, error))
		return FALSE;

	/* get manifest */
	fw_manifest =
	    fu_firmware_get_image_by_idx(fw_cpd, FU_IFWI_CPD_FIRMWARE_IDX_MANIFEST, error);
	if (fw_manifest == NULL)
		return FALSE;
	g_warning("MOO:\n%s", fu_firmware_to_string(fw_cpd));
	g_warning("MOO:\n%s", fu_firmware_to_string(fw_manifest));

	/* parse all the manifest extensions */
	imgs = fu_firmware_get_images(fw_manifest);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_igsc_aux_firmware_parse_extension(self, img, error))
			return FALSE;
	}
	if (!self->has_manifest_ext || self->device_infos->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "missing extensions");
		return FALSE;
	}

	/* parse the info block */
	if (!fu_igsc_aux_firmware_parse_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_igsc_aux_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	return g_steal_pointer(&buf);
}

static gboolean
fu_igsc_aux_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuIgscAuxFirmware *self = FU_IGSC_AUX_FIRMWARE(firmware);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "oem_version", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->oem_version = val;
	}
	tmp = xb_node_query_text(n, "major_version", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->major_version = val;
	}
	tmp = xb_node_query_text(n, "major_vcn", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->major_vcn = val;
	}

	/* success */
	return TRUE;
}

static void
fu_igsc_aux_firmware_init(FuIgscAuxFirmware *self)
{
	self->device_infos =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_igsc_fwdata_device_info4_unref);
}

static void
fu_igsc_aux_firmware_finalize(GObject *object)
{
	FuIgscAuxFirmware *self = FU_IGSC_AUX_FIRMWARE(object);

	g_ptr_array_unref(self->device_infos);

	G_OBJECT_CLASS(fu_igsc_aux_firmware_parent_class)->finalize(object);
}

static void
fu_igsc_aux_firmware_class_init(FuIgscAuxFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_igsc_aux_firmware_finalize;
	firmware_class->parse = fu_igsc_aux_firmware_parse;
	firmware_class->write = fu_igsc_aux_firmware_write;
	firmware_class->build = fu_igsc_aux_firmware_build;
	firmware_class->export = fu_igsc_aux_firmware_export;
}

FuFirmware *
fu_igsc_aux_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IGSC_AUX_FIRMWARE, NULL));
}
