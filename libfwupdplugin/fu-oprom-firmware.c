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

typedef struct __attribute__((packed)) {
	guint16 signature;
	guint16 image_size; /* of 512 bytes */
	guint32 init_func_entry_point;
	guint16 subsystem;
	guint16 machine_type;
	guint16 compression_type;
	guint8 reserved[8];
	guint16 efi_image_offset;
	guint16 pci_header_offset;
	guint16 expansion_header_offset;
} FuOpromFirmwareHeader2;

typedef struct __attribute__((packed)) {
	guint32 signature;
	guint16 vendor_id;
	guint16 device_id;
	guint16 device_list_pointer;
	guint16 structure_length;
	guint8 structure_revision;
	guint8 class_code[3];
	guint16 image_length; /* of 512 bytes */
	guint16 image_revision;
	guint8 code_type;
	guint8 indicator;
	guint16 max_runtime_image_length;
	guint16 conf_util_code_header_pointer;
	guint16 dmtf_clp_entry_point_pointer;
} FuOpromFirmwarePciData;

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

	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuOpromFirmwareHeader2, signature),
				    &signature,
				    G_LITTLE_ENDIAN,
				    error)) {
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
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuOpromFirmwareHeader2, subsystem),
				    &priv->subsystem,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset +
					G_STRUCT_OFFSET(FuOpromFirmwareHeader2, compression_type),
				    &priv->compression_type,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuOpromFirmwareHeader2, machine_type),
				    &priv->machine_type,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* get PCI offset */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset +
					G_STRUCT_OFFSET(FuOpromFirmwareHeader2, pci_header_offset),
				    &pci_header_offset,
				    G_LITTLE_ENDIAN,
				    error))
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
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuOpromFirmwarePciData, signature),
				    &pci_signature,
				    G_LITTLE_ENDIAN,
				    error))
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
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuOpromFirmwarePciData, image_length),
				    &image_length,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (image_length == 0x0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid image length");
		return FALSE;
	}
	fu_firmware_set_size(firmware, image_length * FU_OPROM_FIRMWARE_ALIGN_LEN);

	/* get code type */
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   offset + G_STRUCT_OFFSET(FuOpromFirmwarePciData, code_type),
				   &code_type,
				   error))
		return FALSE;
	fu_firmware_set_idx(firmware, code_type);

	/* get CPD offset */
	if (!fu_memread_uint16_safe(
		buf,
		bufsz,
		G_STRUCT_OFFSET(FuOpromFirmwareHeader2, expansion_header_offset),
		&expansion_header_offset,
		G_LITTLE_ENDIAN,
		error))
		return FALSE;
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
	g_autoptr(GBytes) blob_cpd = NULL;

	/* the smallest each image (and header) can be is 512 bytes */
	image_size += fu_common_align_up(sizeof(FuOpromFirmwareHeader2), FU_FIRMWARE_ALIGNMENT_512);
	blob_cpd = fu_firmware_get_image_by_id_bytes(firmware, "cpd", NULL);
	if (blob_cpd != NULL) {
		image_size +=
		    fu_common_align_up(g_bytes_get_size(blob_cpd), FU_FIRMWARE_ALIGNMENT_512);
	}

	/* write the header */
	fu_byte_array_append_uint16(buf, FU_OPROM_HEADER_SIGNATURE, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, image_size / FU_OPROM_FIRMWARE_ALIGN_LEN, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* init_func_entry_point */
	fu_byte_array_append_uint16(buf, priv->subsystem, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, priv->machine_type, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, priv->compression_type, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint64(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN); /* efi_image_offset */
	fu_byte_array_append_uint16(buf,
				    sizeof(FuOpromFirmwareHeader2),
				    G_LITTLE_ENDIAN); /* pci_header_offset */
	fu_byte_array_append_uint16(buf,
				    blob_cpd != NULL ? image_size - FU_OPROM_FIRMWARE_ALIGN_LEN
						     : 0x0,
				    G_LITTLE_ENDIAN); /* expansion_header_offset */
	g_assert(buf->len == sizeof(FuOpromFirmwareHeader2));

	/* add PCI section */
	fu_byte_array_append_uint32(buf, FU_OPROM_FIRMWARE_PCI_DATA_SIGNATURE, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, 0x8086, G_LITTLE_ENDIAN); /* vendor_id */
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN);	   /* device_id */
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN);	   /* device_list_pointer */
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN);	   /* structure_length */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN);	   /* structure_revision */
	fu_byte_array_append_uint16(buf, image_size / FU_OPROM_FIRMWARE_ALIGN_LEN, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN); /* image_revision */
	fu_byte_array_append_uint8(buf, fu_firmware_get_idx(firmware));
	fu_byte_array_append_uint8(buf, FU_OPROM_FIRMWARE_LAST_IMAGE_INDICATOR_BIT);
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN); /* max_runtime_image_length */
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN); /* conf_util_code_header_pointer */
	fu_byte_array_append_uint16(buf, 0x0, G_LITTLE_ENDIAN); /* dmtf_clp_entry_point_pointer */
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
