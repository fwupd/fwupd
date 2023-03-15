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
	FuStruct *st_hdr = fu_struct_lookup(firmware, "OpromFirmwareHeader2");
	return fu_struct_unpack_full(st_hdr,
				     g_bytes_get_data(fw, NULL),
				     g_bytes_get_size(fw),
				     offset,
				     FU_STRUCT_FLAG_ONLY_CONSTANTS,
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
	FuStruct *st_hdr = fu_struct_lookup(self, "OpromFirmwareHeader2");
	FuStruct *st_pci = fu_struct_lookup(self, "OpromFirmwarePciData");
	guint16 expansion_header_offset = 0;
	guint16 pci_header_offset;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	guint16 image_length = 0;

	/* parse header */
	if (!fu_struct_unpack_full(st_hdr, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
		return FALSE;
	priv->subsystem = fu_struct_get_u16(st_hdr, "subsystem");
	priv->compression_type = fu_struct_get_u16(st_hdr, "compression_type");
	priv->machine_type = fu_struct_get_u16(st_hdr, "machine_type");

	/* get PCI offset */
	pci_header_offset = fu_struct_get_u16(st_hdr, "pci_header_offset");
	if (pci_header_offset == 0x0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "no PCI data structure offset provided");
		return FALSE;
	}

	/* verify signature */
	if (!fu_struct_unpack_full(st_pci,
				   buf,
				   bufsz,
				   offset + pci_header_offset,
				   FU_STRUCT_FLAG_NONE,
				   error))
		return FALSE;

	/* get length */
	image_length = fu_struct_get_u16(st_pci, "image_length");
	if (image_length == 0x0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid image length");
		return FALSE;
	}
	fu_firmware_set_size(firmware, image_length * FU_OPROM_FIRMWARE_ALIGN_LEN);
	fu_firmware_set_idx(firmware, fu_struct_get_u8(st_pci, "code_type"));

	/* get CPD offset */
	expansion_header_offset = fu_struct_get_u16(st_hdr, "expansion_header_offset");
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
	FuStruct *st_hdr = fu_struct_lookup(self, "OpromFirmwareHeader2");
	FuStruct *st_pci = fu_struct_lookup(self, "OpromFirmwarePciData");
	gsize image_size = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob_cpd = NULL;

	/* the smallest each image (and header) can be is 512 bytes */
	image_size += fu_common_align_up(fu_struct_size(st_hdr), FU_FIRMWARE_ALIGNMENT_512);
	blob_cpd = fu_firmware_get_image_by_id_bytes(firmware, "cpd", NULL);
	if (blob_cpd != NULL) {
		image_size +=
		    fu_common_align_up(g_bytes_get_size(blob_cpd), FU_FIRMWARE_ALIGNMENT_512);
	}

	/* write the header */
	fu_struct_set_u16(st_hdr, "image_size", image_size / FU_OPROM_FIRMWARE_ALIGN_LEN);
	fu_struct_set_u16(st_hdr, "subsystem", priv->subsystem);
	fu_struct_set_u16(st_hdr, "machine_type", priv->machine_type);
	fu_struct_set_u16(st_hdr, "compression_type", priv->compression_type);
	fu_struct_set_u16(st_hdr,
			  "expansion_header_offset",
			  blob_cpd != NULL ? image_size - FU_OPROM_FIRMWARE_ALIGN_LEN : 0x0);
	buf = fu_struct_pack(st_hdr);

	/* add PCI section */
	fu_struct_set_u16(st_pci, "vendor_id", 0x8086);
	fu_struct_set_u16(st_pci, "image_length", image_size / FU_OPROM_FIRMWARE_ALIGN_LEN);
	fu_struct_set_u8(st_pci, "code_type", fu_firmware_get_idx(firmware));
	fu_struct_set_u8(st_pci, "indicator", FU_OPROM_FIRMWARE_LAST_IMAGE_INDICATOR_BIT);
	fu_struct_pack_into(st_pci, buf);
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
	fu_struct_register(self,
			   "OpromFirmwareHeader2 {"
			   "    signature: u16le:: 0xAA55,"
			   "    image_size: u16le," /* of 512 bytes */
			   "    init_func_entry_point: u32le,"
			   "    subsystem: u16le,"
			   "    machine_type: u16le,"
			   "    compression_type: u16le,"
			   "    reserved: 8u8,"
			   "    efi_image_offset: u16le,"
			   "    pci_header_offset: u16le: $struct_size,"
			   "    expansion_header_offset: u16le,"
			   "}");
	fu_struct_register(self,
			   "OpromFirmwarePciData {"
			   "    signature: u32le:: 0x52494350,"
			   "    vendor_id: u16le,"
			   "    device_id: u16le,"
			   "    device_list_pointer: u16le,"
			   "    structure_length: u16le,"
			   "    structure_revision: u8,"
			   "    class_code: 3u8,"
			   "    image_length: u16le," /* of 512 bytes */
			   "    image_revision: u16le,"
			   "    code_type: u8,"
			   "    indicator: u8,"
			   "    max_runtime_image_length: u16le,"
			   "    conf_util_code_header_pointer: u16le,"
			   "    dmtf_clp_entry_point_pointer: u16le,"
			   "}");
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
