/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-ifwi-cpd-firmware.h"
#include "fu-mem.h"
#include "fu-oprom-firmware.h"
#include "fu-string.h"
#include "fu-struct.h"

/**
 * FuOpromFirmware:
 *
 * An OptionROM can be found in nearly every PCI device. Multiple OptionROM images may be appended.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint16 machine_type;
	guint16 subsystem;
	guint16 compression_type;
} FuOpromFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuOpromFirmware, fu_oprom_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_oprom_firmware_get_instance_private(o))

#define FU_OPROM_HEADER_SIGNATURE		   0xAA55
#define FU_OPROM_FIRMWARE_PCI_DATA_SIGNATURE	   0x52494350 /* "PCIR" */
#define FU_OPROM_FIRMWARE_ALIGN_LEN		   512u
#define FU_OPROM_FIRMWARE_LAST_IMAGE_INDICATOR_BIT (1u << 7)

/**
 * fu_oprom_firmware_get_machine_type:
 * @self: a #FuFirmware
 *
 * Gets the machine type.
 *
 * Returns: an integer
 *
 * Since: 1.8.2
 **/
guint16
fu_oprom_firmware_get_machine_type(FuOpromFirmware *self)
{
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_OPROM_FIRMWARE(self), G_MAXUINT16);
	return priv->machine_type;
}

/**
 * fu_oprom_firmware_get_subsystem:
 * @self: a #FuFirmware
 *
 * Gets the machine type.
 *
 * Returns: an integer
 *
 * Since: 1.8.2
 **/
guint16
fu_oprom_firmware_get_subsystem(FuOpromFirmware *self)
{
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_OPROM_FIRMWARE(self), G_MAXUINT16);
	return priv->subsystem;
}

/**
 * fu_oprom_firmware_get_compression_type:
 * @self: a #FuFirmware
 *
 * Gets the machine type.
 *
 * Returns: an integer
 *
 * Since: 1.8.2
 **/
guint16
fu_oprom_firmware_get_compression_type(FuOpromFirmware *self)
{
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_OPROM_FIRMWARE(self), G_MAXUINT16);
	return priv->compression_type;
}

static void
fu_oprom_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuOpromFirmware *self = FU_OPROM_FIRMWARE(firmware);
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "machine_type", priv->machine_type);
	fu_xmlb_builder_insert_kx(bn, "subsystem", priv->subsystem);
	fu_xmlb_builder_insert_kx(bn, "compression_type", priv->compression_type);
}

static gboolean
fu_oprom_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	gsize bufsz = 0;
	guint16 signature = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (!fu_memread_uint16_safe(buf, bufsz, offset, &signature, G_LITTLE_ENDIAN, error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (signature != FU_OPROM_HEADER_SIGNATURE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid ROM signature, got 0x%x, expected 0x%x",
			    signature,
			    (guint)FU_OPROM_HEADER_SIGNATURE);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_oprom_firmware_parse(FuFirmware *firmware,
			GBytes *fw,
			gsize offset,
			FwupdInstallFlags flags,
			GError **error)
{
	FuOpromFirmware *self = FU_OPROM_FIRMWARE(firmware);
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	guint16 expansion_header_offset = 0;
	guint16 pci_header_offset = 0;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	guint16 image_length = 0;
	guint32 pci_signature = 0;
	guint8 code_type = 0;

	/* parse header */
	if (!fu_struct_unpack_from("<HHLHHH8xHHH",
				   error,
				   buf,
				   bufsz,
				   &offset,
				   NULL, /* signature */
				   NULL, /* image_size */
				   NULL, /* init_func_entry_point */
				   &priv->subsystem,
				   &priv->machine_type,
				   &priv->compression_type,
				   NULL, /* efi_image_offset */
				   &pci_header_offset,
				   &expansion_header_offset))
		return FALSE;
	if (pci_header_offset == 0x0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "no PCI data structure offset provided");
		return FALSE;
	}

	/* verify signature */
	offset += pci_header_offset;
	if (!fu_struct_unpack_from("<LHHHHB3BHHBBHHH",
				   error,
				   buf,
				   bufsz,
				   &offset,
				   &pci_signature,
				   NULL,	  /* vendor_id */
				   NULL,	  /* device_id */
				   NULL,	  /* device_list_pointer */
				   NULL,	  /* structure_length */
				   NULL,	  /* structure_revision */
				   NULL,	  /* class_code[3] */
				   &image_length, /* of 512 bytes */
				   NULL,	  /* image_revision */
				   &code_type,
				   NULL,  /* indicator */
				   NULL,  /* max_runtime_image_length */
				   NULL,  /* conf_util_code_header_pointer */
				   NULL)) /* dmtf_clp_entry_point_pointer */
		return FALSE;
	if (pci_signature != FU_OPROM_FIRMWARE_PCI_DATA_SIGNATURE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid PCI signature, got 0x%x, expected 0x%x",
			    pci_signature,
			    (guint)FU_OPROM_FIRMWARE_PCI_DATA_SIGNATURE);
		return FALSE;
	}

	/* get length */
	if (image_length == 0x0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid image length");
		return FALSE;
	}
	fu_firmware_set_size(firmware, image_length * FU_OPROM_FIRMWARE_ALIGN_LEN);

	/* get code type */
	fu_firmware_set_idx(firmware, code_type);

	/* get CPD offset */
	if (expansion_header_offset != 0x0) {
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GBytes) blob = NULL;

		blob = fu_bytes_new_offset(fw,
					   expansion_header_offset,
					   bufsz - expansion_header_offset,
					   error);
		if (blob == NULL) {
			g_prefix_error(error, "failed to section CPD: ");
			return FALSE;
		}

		img = fu_firmware_new_from_gtypes(blob,
						  FWUPD_INSTALL_FLAG_NONE,
						  error,
						  FU_TYPE_IFWI_CPD_FIRMWARE,
						  FU_TYPE_FIRMWARE,
						  G_TYPE_INVALID);
		if (img == NULL) {
			g_prefix_error(error, "failed to build firmware: ");
			return FALSE;
		}
		fu_firmware_set_id(img, "cpd");
		fu_firmware_set_offset(img, expansion_header_offset);
		fu_firmware_add_image(firmware, img);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_oprom_firmware_write(FuFirmware *firmware, GError **error)
{
	FuOpromFirmware *self = FU_OPROM_FIRMWARE(firmware);
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize image_size = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) buf_hdr = NULL;
	g_autoptr(GByteArray) buf_sect = NULL;
	g_autoptr(GBytes) blob_cpd = NULL;

	/* the smallest each image (and header) can be is 512 bytes */
	image_size +=
	    fu_common_align_up(fu_struct_size("<HHLHHH8xHHH", NULL), FU_FIRMWARE_ALIGNMENT_512);
	blob_cpd = fu_firmware_get_image_by_id_bytes(firmware, "cpd", NULL);
	if (blob_cpd != NULL) {
		image_size +=
		    fu_common_align_up(g_bytes_get_size(blob_cpd), FU_FIRMWARE_ALIGNMENT_512);
	}

	/* write the header */
	buf_hdr = fu_struct_pack("<HHLHHH8xHHH",
				 error,
				 FU_OPROM_HEADER_SIGNATURE,
				 image_size / FU_OPROM_FIRMWARE_ALIGN_LEN, /* of 512 bytes */
				 0x0, /* init_func_entry_point */
				 priv->subsystem,
				 priv->machine_type,
				 priv->compression_type,
				 0x0,				       /* efi_image_offset */
				 fu_struct_size("<HHLHHH8xHHH", NULL), /* pci_header_offset */
				 blob_cpd != NULL ? image_size - FU_OPROM_FIRMWARE_ALIGN_LEN
						  : 0x0 /* expansion_header_offset */
	);
	if (buf_hdr == NULL)
		return NULL;
	g_byte_array_append(buf, buf_hdr->data, buf_hdr->len);

	/* add PCI section */
	buf_sect = fu_struct_pack("<LHHHHLHHBBHHH",
				  error,
				  FU_OPROM_FIRMWARE_PCI_DATA_SIGNATURE,
				  0x8086, /* vendor_id */
				  0x0,	  /* device_id */
				  0x0,	  /* device_list_pointer */
				  0x0,	  /* structure_length */
				  0x0,	  /* structure_revision */
				  image_size / FU_OPROM_FIRMWARE_ALIGN_LEN,
				  0x0, /* image_revision */
				  (guint)fu_firmware_get_idx(firmware),
				  FU_OPROM_FIRMWARE_LAST_IMAGE_INDICATOR_BIT,
				  0x0, /* max_runtime_image_length */
				  0x0, /* conf_util_code_header_pointer */
				  0x0  /* dmtf_clp_entry_point_pointer */
	);
	g_byte_array_append(buf, buf_sect->data, buf_sect->len);
	fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_512, 0xFF);

	/* add CPD */
	if (blob_cpd != NULL) {
		fu_byte_array_append_bytes(buf, blob_cpd);
		fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_512, 0xFF);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_oprom_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuOpromFirmware *self = FU_OPROM_FIRMWARE(firmware);
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "machine_type", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, error))
			return FALSE;
		priv->machine_type = val;
	}
	tmp = xb_node_query_text(n, "subsystem", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, error))
			return FALSE;
		priv->subsystem = val;
	}
	tmp = xb_node_query_text(n, "compression_type", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, error))
			return FALSE;
		priv->compression_type = val;
	}

	/* success */
	return TRUE;
}

static void
fu_oprom_firmware_init(FuOpromFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
}

static void
fu_oprom_firmware_class_init(FuOpromFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_oprom_firmware_check_magic;
	klass_firmware->export = fu_oprom_firmware_export;
	klass_firmware->parse = fu_oprom_firmware_parse;
	klass_firmware->write = fu_oprom_firmware_write;
	klass_firmware->build = fu_oprom_firmware_build;
}

/**
 * fu_oprom_firmware_new:
 *
 * Creates a new #FuFirmware of OptionROM format
 *
 * Since: 1.8.2
 **/
FuFirmware *
fu_oprom_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_OPROM_FIRMWARE, NULL));
}
