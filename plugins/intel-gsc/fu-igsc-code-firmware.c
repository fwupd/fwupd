/*
 * Copyright (C) 2022 Intel
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-igsc-code-firmware.h"

struct _FuIgscCodeFirmware {
	FuIfwiFptFirmware parent_instance;
	guint32 hw_sku;
};

G_DEFINE_TYPE(FuIgscCodeFirmware, fu_igsc_code_firmware, FU_TYPE_IFWI_FPT_FIRMWARE)

#define GSC_FWU_IUP_NUM				   2
#define FU_IGSC_FIRMWARE_MAX_SIZE		   (8 * 1024 * 1024) /* 8M */
#define FU_IGSC_FIRMWARE_IMAGE_INFO_FORMAT_VERSION 0x1

struct gsc_fwu_external_version {
	char project[4];
	guint16 hotfix;
	guint16 build;
} __attribute__((packed));

struct gsc_fwu_heci_image_metadata {
	guint32 metadata_format_version; /* meta data version */
} __attribute__((packed));

struct gsc_fwu_version {
	guint16 major;
	guint16 minor;
	guint16 hotfix;
	guint16 build;
} __attribute__((packed));

struct fwu_gws_image_info {
	guint32 format_version;
	guint32 instance_id;
	guint32 reserved[14];
} __attribute__((packed));

/* represents a GSC FW sub-partition such as FTPR, RBEP */
struct gsc_fwu_fw_image_data {
	struct gsc_fwu_version fw_version;
	guint16 flags;
	guint8 fw_type;
	guint8 fw_sub_type;
	guint32 arb_svn;
	guint32 tcb_svn;
	guint32 vcn;
} __attribute__((packed));

struct gsc_fwu_iup_data {
	guint32 iup_name;
	guint16 flags;
	guint16 reserved;
	guint32 svn;
	guint32 vcn;
} __attribute__((packed));

struct gsc_fwu_image_data {
	struct gsc_fwu_fw_image_data fw_img_data;	   /* FTPR data */
	struct gsc_fwu_iup_data iup_data[GSC_FWU_IUP_NUM]; /* IUP Data */
} __attribute__((packed));

struct gsc_fwu_image_metadata_v1 {
	struct gsc_fwu_external_version overall_version; /* The version of the overall IFWI image,
							    i.e. the combination of IPs */
	struct gsc_fwu_image_data update_img_data;	 /* Sub-partitions */
} __attribute__((packed));

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
fu_igsc_code_firmware_parse_imgi(FuIgscCodeFirmware *self, GBytes *fw, GError **error)
{
	gsize bufsz = 0;
	guint32 format_version = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* the command is only supported on DG2 */
	if (g_strcmp0(fu_firmware_get_id(FU_FIRMWARE(self)), "DG02") != 0)
		return TRUE;

	/* get hw_sku */
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    G_STRUCT_OFFSET(struct fwu_gws_image_info, format_version),
				    &format_version,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (format_version != FU_IGSC_FIRMWARE_IMAGE_INFO_FORMAT_VERSION) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "wrong image info format version, got 0x%x, expected 0x%x: ",
			    format_version,
			    (guint)FU_IGSC_FIRMWARE_IMAGE_INFO_FORMAT_VERSION);
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    G_STRUCT_OFFSET(struct fwu_gws_image_info, instance_id),
				    &self->hw_sku,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_igsc_code_firmware_parse(FuFirmware *firmware,
			    GBytes *fw,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuIgscCodeFirmware *self = FU_IGSC_CODE_FIRMWARE(firmware);
	const guint8 *buf;
	gsize bufsz = 0;
	struct gsc_fwu_heci_image_metadata metadata_hdr = {0x0};
	struct gsc_fwu_image_metadata_v1 metadata_v1 = {0x0};
	g_autofree gchar *project = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(GBytes) fw_info = NULL;
	g_autoptr(GBytes) fw_imgi = NULL;

	/* sanity check */
	if (g_bytes_get_size(fw) > FU_IGSC_FIRMWARE_MAX_SIZE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "image size too big: 0x%x",
			    (guint)g_bytes_get_size(fw));
		return FALSE;
	}

	/* FuIfwiFptFirmware->parse */
	if (!FU_FIRMWARE_CLASS(fu_igsc_code_firmware_parent_class)
		 ->parse(firmware, fw, offset, flags, error))
		return FALSE;

	fw_info = fu_firmware_get_image_by_idx_bytes(FU_FIRMWARE(self),
						     FU_IFWI_FPT_FIRMWARE_IDX_INFO,
						     error);
	if (fw_info == NULL)
		return FALSE;

	/* check metadata header format */
	buf = g_bytes_get_data(fw_info, &bufsz);
	if (!fu_memcpy_safe((guint8 *)&metadata_hdr,
			    sizeof(metadata_hdr),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    0x0, /* src */
			    sizeof(metadata_hdr),
			    error)) {
		return FALSE;
	}
	if (metadata_hdr.metadata_format_version != 0x01) {
		/* Note that it's still ok to use the V1 metadata struct to get the
		 * FW version because the FW version position and structure stays
		 * the same in all versions of the struct */
		g_warning("metadata format version is %u, instead of expected V1",
			  metadata_hdr.metadata_format_version);
	}

	/* copy actual header */
	if (!fu_memcpy_safe((guint8 *)&metadata_v1,
			    sizeof(metadata_v1),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    sizeof(metadata_hdr), /* src */
			    sizeof(metadata_v1),
			    error)) {
		return FALSE;
	}
	project = g_strdup_printf("%c%c%c%c",
				  metadata_v1.overall_version.project[0],
				  metadata_v1.overall_version.project[1],
				  metadata_v1.overall_version.project[2],
				  metadata_v1.overall_version.project[3]);
	fu_firmware_set_id(FU_FIRMWARE(self), project);
	version = g_strdup_printf("%04d.%04d",
				  metadata_v1.overall_version.hotfix,
				  metadata_v1.overall_version.build);
	fu_firmware_set_version(FU_FIRMWARE(self), version);

	/* get instance ID for image */
	fw_imgi = fu_firmware_get_image_by_idx_bytes(FU_FIRMWARE(self),
						     FU_IFWI_FPT_FIRMWARE_IDX_IMGI,
						     error);
	if (fw_imgi == NULL)
		return FALSE;
	if (!fu_igsc_code_firmware_parse_imgi(self, fw_imgi, error))
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_igsc_code_firmware_parse;
	klass_firmware->export = fu_igsc_code_firmware_export;
}

FuFirmware *
fu_igsc_code_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IGSC_CODE_FIRMWARE, NULL));
}
