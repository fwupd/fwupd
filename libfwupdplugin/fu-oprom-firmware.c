/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-ifwi-cpd-firmware.h"
#include "fu-input-stream.h"
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
	FuOpromMachineType machine_type;
	FuOpromSubsystem subsystem;
	FuOpromCompressionType compression_type;
	guint16 vendor_id;
	guint16 device_id;
} FuOpromFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuOpromFirmware, fu_oprom_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_oprom_firmware_get_instance_private(o))

#define FU_OPROM_FIRMWARE_ALIGN_LEN 512u

/**
 * fu_oprom_firmware_get_machine_type:
 * @self: a #FuFirmware
 *
 * Gets the machine type.
 *
 * Returns: a #FuOpromMachineType
 **/
FuOpromMachineType
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
 * Returns: a #FuOpromSubsystem
 **/
FuOpromSubsystem
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
 * Returns: a #FuOpromCompressionType
 **/
FuOpromCompressionType
fu_oprom_firmware_get_compression_type(FuOpromFirmware *self)
{
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_OPROM_FIRMWARE(self), G_MAXUINT16);
	return priv->compression_type;
}

static void
fu_oprom_firmware_export(FuFirmware *firmware,
			 FuFirmwareExportFlags flags,
			 FwupdJsonObject *json_object)
{
	FuOpromFirmware *self = FU_OPROM_FIRMWARE(firmware);
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	if (priv->machine_type != 0)
		fwupd_json_object_add_integer(json_object, "machine_type", priv->machine_type);
	if (priv->subsystem != 0)
		fwupd_json_object_add_integer(json_object, "subsystem", priv->subsystem);
	if (priv->compression_type != 0)
		fwupd_json_object_add_integer(json_object,
					      "compression_type",
					      priv->compression_type);
	if (priv->vendor_id != 0)
		fwupd_json_object_add_integer(json_object, "vendor_id", priv->vendor_id);
	if (priv->device_id != 0)
		fwupd_json_object_add_integer(json_object, "device_id", priv->device_id);
}

static gboolean
fu_oprom_firmware_validate(FuFirmware *firmware,
			   FuInputStream *stream,
			   gsize offset,
			   GError **error)
{
	return fu_struct_oprom_validate_stream(stream, offset, error);
}

static gboolean
fu_oprom_firmware_parse(FuFirmware *firmware,
			FuInputStream *stream,
			FuFirmwareParseFlags flags,
			GError **error)
{
	FuOpromFirmware *self = FU_OPROM_FIRMWARE(firmware);
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);
	guint16 expansion_header_offset = 0;
	guint16 pci_header_offset;
	guint16 image_length = 0;
	gsize image_size = 0;
	g_autoptr(FuStructOprom) st_hdr = NULL;
	g_autoptr(FuStructOpromPci) st_pci = NULL;

	/* parse header */
	st_hdr = fu_struct_oprom_parse_stream(stream, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;
	priv->subsystem = fu_struct_oprom_get_subsystem(st_hdr);
	priv->compression_type = fu_struct_oprom_get_compression_type(st_hdr);
	priv->machine_type = fu_struct_oprom_get_machine_type(st_hdr);

	/* get PCI offset */
	pci_header_offset = fu_struct_oprom_get_pci_header_offset(st_hdr);
	if (pci_header_offset == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no PCI data structure offset provided");
		return FALSE;
	}

	/* verify signature */
	st_pci = fu_struct_oprom_pci_parse_stream(stream, pci_header_offset, error);
	if (st_pci == NULL)
		return FALSE;
	priv->vendor_id = fu_struct_oprom_pci_get_vendor_id(st_pci);
	priv->device_id = fu_struct_oprom_pci_get_device_id(st_pci);

	/* get length */
	image_length = fu_struct_oprom_pci_get_image_length(st_pci);
	if (image_length == 0x0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid image");
		return FALSE;
	}
	if (!fu_size_checked_inc_product(&image_size,
					 image_length,
					 FU_OPROM_FIRMWARE_ALIGN_LEN,
					 error)) {
		g_prefix_error_literal(error, "image size overflow: ");
		return FALSE;
	}
	fu_firmware_set_size(firmware, image_size);
	fu_firmware_set_idx(firmware, fu_struct_oprom_pci_get_code_type(st_pci));

	/* is last image */
	if (fu_struct_oprom_pci_get_indicator(st_pci) & FU_OPROM_INDICATOR_FLAG_LAST)
		fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_IS_LAST_IMAGE);

	/* get CPD offset */
	expansion_header_offset = fu_struct_oprom_get_expansion_header_offset(st_hdr);
	if (expansion_header_offset != 0x0) {
		g_autoptr(FuFirmware) img = NULL;
		img = fu_firmware_new_from_gtypes(stream,
						  expansion_header_offset,
						  flags,
						  error,
						  FU_TYPE_IFWI_CPD_FIRMWARE,
						  FU_TYPE_FIRMWARE,
						  G_TYPE_INVALID);
		if (img == NULL) {
			g_prefix_error_literal(error, "failed to build firmware: ");
			return FALSE;
		}
		fu_firmware_set_id(img, "cpd");
		fu_firmware_set_offset(img, expansion_header_offset);
		if (!fu_firmware_add_image(firmware, img, error))
			return FALSE;
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
	g_autoptr(FuStructOprom) st_hdr = fu_struct_oprom_new();
	g_autoptr(FuStructOpromPci) st_pci = fu_struct_oprom_pci_new();
	g_autoptr(GBytes) blob_cpd = NULL;

	/* the smallest each image (and header) can be is 512 bytes */
	image_size = fu_common_align_up(st_hdr->buf->len, FU_FIRMWARE_ALIGNMENT_512);
	blob_cpd = fu_firmware_get_image_by_id_bytes(firmware, "cpd", NULL);
	if (blob_cpd != NULL) {
		gsize cpd_size =
		    fu_common_align_up(g_bytes_get_size(blob_cpd), FU_FIRMWARE_ALIGNMENT_512);
		if (!fu_size_checked_inc(&image_size, cpd_size, error)) {
			g_prefix_error_literal(error, "overflow calculating OptionROM size: ");
			return NULL;
		}
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
	g_byte_array_append(buf, st_hdr->buf->data, st_hdr->buf->len);

	/* add PCI section */
	fu_struct_oprom_pci_set_vendor_id(st_pci, priv->vendor_id);
	fu_struct_oprom_pci_set_device_id(st_pci, priv->device_id);
	fu_struct_oprom_pci_set_image_length(st_pci, image_size / FU_OPROM_FIRMWARE_ALIGN_LEN);
	fu_struct_oprom_pci_set_code_type(st_pci, fu_firmware_get_idx(firmware));
	if (fu_firmware_has_flag(firmware, FU_FIRMWARE_FLAG_IS_LAST_IMAGE))
		fu_struct_oprom_pci_set_indicator(st_pci, FU_OPROM_INDICATOR_FLAG_LAST);
	fu_byte_array_append_array(buf, st_pci->buf);
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
fu_oprom_firmware_build(FuFirmware *firmware, FwupdJsonObject *json_object, GError **error)
{
	FuOpromFirmware *self = FU_OPROM_FIRMWARE(firmware);
	FuOpromFirmwarePrivate *priv = GET_PRIVATE(self);

	/* simple properties */
	if (fwupd_json_object_has_node(json_object, "machine_type")) {
		gint64 val = G_MAXINT64;
		if (!fwupd_json_object_get_integer(json_object, "machine_type", &val, error))
			return FALSE;
		if (val < 0 || val > G_MAXUINT16) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid machine_type");
			return FALSE;
		}
		priv->machine_type = val;
	}
	if (fwupd_json_object_has_node(json_object, "subsystem")) {
		gint64 val = G_MAXINT64;
		if (!fwupd_json_object_get_integer(json_object, "subsystem", &val, error))
			return FALSE;
		if (val < 0 || val > G_MAXUINT16) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid subsystem");
			return FALSE;
		}
		priv->subsystem = val;
	}
	if (fwupd_json_object_has_node(json_object, "compression_type")) {
		gint64 val = G_MAXINT64;
		if (!fwupd_json_object_get_integer(json_object, "compression_type", &val, error))
			return FALSE;
		if (val < 0 || val > G_MAXUINT16) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid compression_type");
			return FALSE;
		}
		priv->compression_type = val;
	}
	if (fwupd_json_object_has_node(json_object, "vendor_id")) {
		gint64 val = G_MAXINT64;
		if (!fwupd_json_object_get_integer(json_object, "vendor_id", &val, error))
			return FALSE;
		if (val < 0 || val > G_MAXUINT16) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid vendor_id");
			return FALSE;
		}
		priv->vendor_id = val;
	}
	if (fwupd_json_object_has_node(json_object, "device_id")) {
		gint64 val = G_MAXINT64;
		if (!fwupd_json_object_get_integer(json_object, "device_id", &val, error))
			return FALSE;
		if (val < 0 || val > G_MAXUINT16) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid device_id");
			return FALSE;
		}
		priv->device_id = val;
	}

	/* success */
	return TRUE;
}

static void
fu_oprom_firmware_init(FuOpromFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALLOW_LINEAR);
}

static void
fu_oprom_firmware_class_init(FuOpromFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_FIRMWARE);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_IFWI_CPD_FIRMWARE);
	fu_firmware_set_size_max(firmware_class, 128 * FU_MB);
	firmware_class->validate = fu_oprom_firmware_validate;
	firmware_class->export = fu_oprom_firmware_export;
	firmware_class->parse = fu_oprom_firmware_parse;
	firmware_class->write = fu_oprom_firmware_write;
	firmware_class->build = fu_oprom_firmware_build;
}

/**
 * fu_oprom_firmware_new:
 *
 * Creates a new #FuFirmware of OptionROM format
 **/
FuFirmware *
fu_oprom_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_OPROM_FIRMWARE, NULL));
}
