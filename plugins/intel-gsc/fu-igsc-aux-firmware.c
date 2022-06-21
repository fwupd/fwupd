/*
 * Copyright (C) 2022 Intel
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-igsc-aux-firmware.h"
#include "fu-igsc-heci.h"

struct _FuIgscAuxFirmware {
	FuIfwiFptFirmware parent_instance;
	guint32 oem_version;
	guint16 major_version;
	guint16 major_vcn;
	GPtrArray *device_infos; /* of igsc_fwdata_device_info */
	gboolean has_manifest_ext;
};

G_DEFINE_TYPE(FuIgscAuxFirmware, fu_igsc_aux_firmware, FU_TYPE_IFWI_FPT_FIRMWARE)

#define MFT_EXT_TYPE_DEVICE_IDS	   37
#define MFT_EXT_TYPE_FWDATA_UPDATE 29

struct mft_fwdata_update_ext {
	guint32 extension_type;
	guint32 extension_length;
	guint32 oem_manuf_data_version;
	guint16 major_vcn;
	guint16 flags;
};

struct igsc_fwdata_device_info {
	guint16 vendor_id;
	guint16 device_id;
	guint16 subsys_vendor_id;
	guint16 subsys_device_id;
};

struct igsc_fwdata_version {
	guint32 oem_manuf_data_version;
	guint16 major_version;
	guint16 major_vcn;
};

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
		struct igsc_fwdata_device_info *info = g_ptr_array_index(self->device_infos, i);
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
	gsize bufsz = 0;
	const guint8 *buf;
	struct igsc_fwdata_version version = {0x0};
	g_autoptr(GBytes) fw_info = NULL;

	fw_info = fu_firmware_get_image_by_idx_bytes(FU_FIRMWARE(self),
						     FU_IFWI_FPT_FIRMWARE_IDX_SDTA,
						     error);
	if (fw_info == NULL)
		return FALSE;
	buf = g_bytes_get_data(fw_info, &bufsz);

	if (!fu_memcpy_safe((guint8 *)&version,
			    sizeof(version),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    sizeof(struct gsc_fwu_heci_image_metadata), /* src */
			    sizeof(version),
			    error)) {
		g_prefix_error(error, "no version: ");
		return FALSE;
	}
	self->oem_version = version.oem_manuf_data_version;
	self->major_vcn = version.major_vcn;
	self->major_version = version.major_version;
	return TRUE;
}

static gboolean
fu_igsc_aux_firmware_parse_extension(FuIgscAuxFirmware *self, FuFirmware *fw, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	g_autoptr(GBytes) blob = NULL;

	/* get data */
	blob = fu_firmware_get_bytes(fw, error);
	if (blob == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob, &bufsz);

	if (fu_firmware_get_idx(fw) == MFT_EXT_TYPE_DEVICE_IDS) {
		for (gsize offset = 0; offset < bufsz;
		     offset += sizeof(struct igsc_fwdata_device_info)) {
			struct igsc_fwdata_device_info device_info = {0x0};
			if (!fu_memcpy_safe((guint8 *)&device_info,
					    sizeof(device_info),
					    0x0, /* dst */
					    buf,
					    bufsz,
					    offset, /* src */
					    sizeof(device_info),
					    error)) {
				g_prefix_error(error, "no ext header: ");
				return FALSE;
			}
			g_ptr_array_add(self->device_infos,
					fu_memdup_safe((const guint8 *)&device_info,
						       sizeof(device_info),
						       NULL));
		}
	} else if (fu_firmware_get_idx(fw) == MFT_EXT_TYPE_FWDATA_UPDATE) {
		if (bufsz != sizeof(struct mft_fwdata_update_ext)) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "signed data update manifest ext was 0x%x bytes",
				    (guint)bufsz);
			return FALSE;
		}
		self->has_manifest_ext = TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_igsc_aux_firmware_parse(FuFirmware *firmware,
			   GBytes *fw,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuIgscAuxFirmware *self = FU_IGSC_AUX_FIRMWARE(firmware);
	g_autoptr(FuFirmware) fw_cpd = fu_ifwi_cpd_firmware_new();
	g_autoptr(FuFirmware) fw_manifest = NULL;
	g_autoptr(GBytes) blob_dataimg = NULL;
	g_autoptr(GPtrArray) imgs = NULL;

	/* FuIfwiFptFirmware->parse */
	if (!FU_FIRMWARE_CLASS(fu_igsc_aux_firmware_parent_class)
		 ->parse(firmware, fw, offset, flags, error))
		return FALSE;

	/* parse data section */
	blob_dataimg =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_IFWI_FPT_FIRMWARE_IDX_SDTA, error);
	if (blob_dataimg == NULL)
		return FALSE;

	/* parse as CPD */
	if (!fu_firmware_parse(fw_cpd, blob_dataimg, flags, error))
		return FALSE;

	/* get manifest */
	fw_manifest =
	    fu_firmware_get_image_by_idx(fw_cpd, FU_IFWI_CPD_FIRMWARE_IDX_MANIFEST, error);
	if (fw_manifest == NULL)
		return FALSE;

	/* parse all the manifets extensions */
	imgs = fu_firmware_get_images(fw_manifest);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_igsc_aux_firmware_parse_extension(self, img, error))
			return FALSE;
	}
	if (!self->has_manifest_ext || self->device_infos->len == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "missing extensions");
		return FALSE;
	}

	/* parse the info block */
	if (!fu_igsc_aux_firmware_parse_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static GBytes *
fu_igsc_aux_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
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
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT32, error))
			return FALSE;
		self->oem_version = val;
	}
	tmp = xb_node_query_text(n, "major_version", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, error))
			return FALSE;
		self->major_version = val;
	}
	tmp = xb_node_query_text(n, "major_vcn", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, error))
			return FALSE;
		self->major_vcn = val;
	}

	/* success */
	return TRUE;
}

static void
fu_igsc_aux_firmware_init(FuIgscAuxFirmware *self)
{
	self->device_infos = g_ptr_array_new_with_free_func(g_free);
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_igsc_aux_firmware_finalize;
	klass_firmware->parse = fu_igsc_aux_firmware_parse;
	klass_firmware->write = fu_igsc_aux_firmware_write;
	klass_firmware->build = fu_igsc_aux_firmware_build;
	klass_firmware->export = fu_igsc_aux_firmware_export;
}

FuFirmware *
fu_igsc_aux_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IGSC_AUX_FIRMWARE, NULL));
}
