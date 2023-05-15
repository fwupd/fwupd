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
#include "fu-oprom-firmware.h"
#include "fu-oprom-struct.h"
#include "fu-string.h"

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
	return fu_struct_oprom_validate(g_bytes_get_data(fw, NULL),
					g_bytes_get_size(fw),
					offset,
					error);
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
	guint16 pci_header_offset;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	guint16 image_length = 0;
	g_autoptr(GByteArray) st_hdr = NULL;
	g_autoptr(GByteArray) st_pci = NULL;

	/* parse header */
	st_hdr = fu_struct_oprom_parse(buf, bufsz, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	priv->subsystem = fu_struct_oprom_get_subsystem(st_hdr);
	priv->compression_type = fu_struct_oprom_get_compression_type(st_hdr);
	priv->machine_type = fu_struct_oprom_get_machine_type(st_hdr);

	/* get PCI offset */
	pci_header_offset = fu_struct_oprom_get_pci_header_offset(st_hdr);
	if (pci_header_offset == 0x0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "no PCI data structure offset provided");
		return FALSE;
	}

	/* verify signature */
	st_pci = fu_struct_oprom_pci_parse(buf, bufsz, offset + pci_header_offset, error);
	if (st_pci == NULL)
		return FALSE;

	/* get length */
	image_length = fu_struct_oprom_pci_get_image_length(st_pci);
	if (image_length == 0x0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid image length");
		return FALSE;
	}
	fu_firmware_set_size(firmware, image_length * FU_OPROM_FIRMWARE_ALIGN_LEN);
	fu_firmware_set_idx(firmware, fu_struct_oprom_pci_get_code_type(st_pci));

	/* get CPD offset */
	expansion_header_offset = fu_struct_oprom_get_expansion_header_offset(st_hdr);
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

static GByteArray *
fu_oprom_firmware_write(FuFirmware *firmware, GError **error)
{
	FuOpromFirmware *self = FU_OPROM_FIRMWARE(firmware);
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize image_size = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) st_hdr = fu_struct_oprom_new();
	g_autoptr(GByteArray) st_pci = fu_struct_oprom_pci_new();
	g_autoptr(GBytes) blob_cpd = NULL;

	/* the smallest each image (and header) can be is 512 bytes */
	image_size += fu_common_align_up(st_hdr->len, FU_FIRMWARE_ALIGNMENT_512);
	blob_cpd = fu_firmware_get_image_by_id_bytes(firmware, "cpd", NULL);
	if (blob_cpd != NULL) {
		image_size +=
		    fu_common_align_up(g_bytes_get_size(blob_cpd), FU_FIRMWARE_ALIGNMENT_512);
	}

	/* write the header */
	fu_struct_oprom_set_image_size(st_hdr, image_size / FU_OPROM_FIRMWARE_ALIGN_LEN);
	fu_struct_oprom_set_subsystem(st_hdr, priv->subsystem);
	fu_struct_oprom_set_machine_type(st_hdr, priv->machine_type);
	fu_struct_oprom_set_compression_type(st_hdr, priv->compression_type);
	if (blob_cpd != NULL) {
		fu_struct_oprom_set_expansion_header_offset(st_hdr,
							    image_size -
								FU_OPROM_FIRMWARE_ALIGN_LEN);
	}
	g_byte_array_append(buf, st_hdr->data, st_hdr->len);

	/* add PCI section */
	fu_struct_oprom_pci_set_vendor_id(st_pci, 0x8086);
	fu_struct_oprom_pci_set_image_length(st_pci, image_size / FU_OPROM_FIRMWARE_ALIGN_LEN);
	fu_struct_oprom_pci_set_code_type(st_pci, fu_firmware_get_idx(firmware));
	fu_struct_oprom_pci_set_indicator(st_pci, FU_OPROM_FIRMWARE_LAST_IMAGE_INDICATOR_BIT);
	g_byte_array_append(buf, st_pci->data, st_pci->len);
	fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_512, 0xFF);

	/* add CPD */
	if (blob_cpd != NULL) {
		fu_byte_array_append_bytes(buf, blob_cpd);
		fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_512, 0xFF);
	}

	/* success */
	return g_steal_pointer(&buf);
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
