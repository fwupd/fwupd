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
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-ifwi-cpd-firmware.h"
#include "fu-ifwi-struct.h"
#include "fu-input-stream.h"
#include "fu-partial-input-stream.h"
#include "fu-string.h"

/**
 * FuIfwiCpdFirmware:
 *
 * An Intel Code Partition Directory (aka CPD) can be found in IFWI (Integrated Firmware Image)
 * firmware blobs which are used in various Intel products using an IPU (Infrastructure Processing
 * Unit).
 *
 * This could include hardware like SmartNICs, GPUs, camera and audio devices.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint8 header_version;
	guint8 entry_version;
} FuIfwiCpdFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuIfwiCpdFirmware, fu_ifwi_cpd_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_ifwi_cpd_firmware_get_instance_private(o))

#define FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX 1024

static void
fu_ifwi_cpd_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIfwiCpdFirmware *self = FU_IFWI_CPD_FIRMWARE(firmware);
	FuIfwiCpdFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "header_version", priv->header_version);
	fu_xmlb_builder_insert_kx(bn, "entry_version", priv->entry_version);
}

static gboolean
fu_ifwi_cpd_firmware_parse_manifest(FuFirmware *firmware, GInputStream *stream, GError **error)
{
	gsize streamsz = 0;
	guint32 size;
	gsize offset = 0;
	g_autoptr(GByteArray) st_mhd = NULL;

	/* raw version */
	st_mhd = fu_struct_ifwi_cpd_manifest_parse_stream(stream, offset, error);
	if (st_mhd == NULL)
		return FALSE;
	fu_firmware_set_version_raw(firmware, fu_struct_ifwi_cpd_manifest_get_version(st_mhd));

	/* verify the size */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	size = fu_struct_ifwi_cpd_manifest_get_size(st_mhd);
	if (size * 4 != streamsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid manifest invalid length, got 0x%x, expected 0x%x",
			    size * 4,
			    (guint)streamsz);
		return FALSE;
	}

	/* parse extensions */
	offset += fu_struct_ifwi_cpd_manifest_get_header_length(st_mhd) * 4;
	while (offset < streamsz) {
		guint32 extension_type = 0;
		guint32 extension_length = 0;
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(GByteArray) st_mex = NULL;
		g_autoptr(GInputStream) partial_stream = NULL;

		/* set the extension type as the index */
		st_mex = fu_struct_ifwi_cpd_manifest_ext_parse_stream(stream, offset, error);
		if (st_mex == NULL)
			return FALSE;
		extension_type = fu_struct_ifwi_cpd_manifest_ext_get_extension_type(st_mex);
		if (extension_type == 0x0)
			break;
		fu_firmware_set_idx(img, extension_type);

		/* add data section */
		extension_length = fu_struct_ifwi_cpd_manifest_ext_get_extension_length(st_mex);
		if (extension_length == 0x0)
			break;
		if (extension_length < st_mex->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid manifest extension header length 0x%x",
				    (guint)extension_length);
			return FALSE;
		}
		partial_stream = fu_partial_input_stream_new(stream,
							     offset + st_mex->len,
							     extension_length - st_mex->len,
							     error);
		if (partial_stream == NULL) {
			g_prefix_error(error, "failed to cut CPD extension: ");
			return FALSE;
		}
		if (!fu_firmware_parse_stream(img,
					      partial_stream,
					      0x0,
					      FWUPD_INSTALL_FLAG_NONE,
					      error))
			return FALSE;

		/* success */
		fu_firmware_set_offset(img, offset);
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;
		offset += extension_length;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ifwi_cpd_firmware_validate(FuFirmware *firmware,
			      GInputStream *stream,
			      gsize offset,
			      GError **error)
{
	return fu_struct_ifwi_cpd_validate_stream(stream, offset, error);
}

static gboolean
fu_ifwi_cpd_firmware_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuIfwiCpdFirmware *self = FU_IFWI_CPD_FIRMWARE(firmware);
	FuIfwiCpdFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) st_hdr = NULL;
	gsize offset = 0;
	guint32 num_of_entries;

	/* other header fields */
	st_hdr = fu_struct_ifwi_cpd_parse_stream(stream, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	priv->header_version = fu_struct_ifwi_cpd_get_header_version(st_hdr);
	priv->entry_version = fu_struct_ifwi_cpd_get_entry_version(st_hdr);
	fu_firmware_set_idx(firmware, fu_struct_ifwi_cpd_get_partition_name(st_hdr));

	/* read out entries */
	num_of_entries = fu_struct_ifwi_cpd_get_num_of_entries(st_hdr);
	if (num_of_entries > FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "too many entries 0x%x, expected <= 0x%x",
			    num_of_entries,
			    (guint)FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX);
		return FALSE;
	}
	offset += fu_struct_ifwi_cpd_get_header_length(st_hdr);
	for (guint32 i = 0; i < num_of_entries; i++) {
		guint32 img_offset = 0;
		g_autofree gchar *id = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(GByteArray) st_ent = NULL;
		g_autoptr(GInputStream) partial_stream = NULL;

		/* the IDX is the position in the file */
		fu_firmware_set_idx(img, i);

		st_ent = fu_struct_ifwi_cpd_entry_parse_stream(stream, offset, error);
		if (st_ent == NULL)
			return FALSE;

		/* copy name as id */
		id = fu_struct_ifwi_cpd_entry_get_name(st_ent);
		fu_firmware_set_id(img, id);

		/* copy offset, ignoring huffman and reserved bits */
		img_offset = fu_struct_ifwi_cpd_entry_get_offset(st_ent);
		img_offset &= 0x1FFFFFF;
		fu_firmware_set_offset(img, img_offset);

		/* copy data */
		partial_stream =
		    fu_partial_input_stream_new(stream,
						img_offset,
						fu_struct_ifwi_cpd_entry_get_length(st_ent),
						error);
		if (partial_stream == NULL) {
			g_prefix_error(error, "failed to cut IFD image: ");
			return FALSE;
		}
		if (!fu_firmware_parse_stream(img, partial_stream, 0x0, flags, error))
			return FALSE;

		/* read the manifest */
		if (i == FU_IFWI_CPD_FIRMWARE_IDX_MANIFEST &&
		    fu_struct_ifwi_cpd_entry_get_length(st_ent) >
			FU_STRUCT_IFWI_CPD_MANIFEST_SIZE) {
			if (!fu_ifwi_cpd_firmware_parse_manifest(img, partial_stream, error))
				return FALSE;
		}

		/* success */
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;
		offset += st_ent->len;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_ifwi_cpd_firmware_write(FuFirmware *firmware, GError **error)
{
	FuIfwiCpdFirmware *self = FU_IFWI_CPD_FIRMWARE(firmware);
	FuIfwiCpdFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize offset = 0;
	g_autoptr(GByteArray) buf = fu_struct_ifwi_cpd_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* write the header */
	fu_struct_ifwi_cpd_set_num_of_entries(buf, imgs->len);
	fu_struct_ifwi_cpd_set_header_version(buf, priv->header_version);
	fu_struct_ifwi_cpd_set_entry_version(buf, priv->entry_version);
	fu_struct_ifwi_cpd_set_checksum(buf, 0x0);
	fu_struct_ifwi_cpd_set_partition_name(buf, fu_firmware_get_idx(firmware));
	fu_struct_ifwi_cpd_set_crc32(buf, 0x0);

	/* fixup the image offsets */
	offset += buf->len;
	offset += FU_STRUCT_IFWI_CPD_ENTRY_SIZE * imgs->len;
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = NULL;

		blob = fu_firmware_get_bytes(img, error);
		if (blob == NULL) {
			g_prefix_error(error, "image 0x%x: ", i);
			return NULL;
		}
		fu_firmware_set_offset(img, offset);
		offset += g_bytes_get_size(blob);
	}

	/* add entry headers */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GByteArray) st_ent = fu_struct_ifwi_cpd_entry_new();

		/* sanity check */
		if (fu_firmware_get_id(img) == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "image 0x%x must have an ID",
				    (guint)fu_firmware_get_idx(img));
			return NULL;
		}
		if (!fu_struct_ifwi_cpd_entry_set_name(st_ent, fu_firmware_get_id(img), error))
			return NULL;
		fu_struct_ifwi_cpd_entry_set_offset(st_ent, fu_firmware_get_offset(img));
		fu_struct_ifwi_cpd_entry_set_length(st_ent, fu_firmware_get_size(img));
		g_byte_array_append(buf, st_ent->data, st_ent->len);
	}

	/* add entry data */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = NULL;
		blob = fu_firmware_get_bytes(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, blob);
	}

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_ifwi_cpd_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuIfwiCpdFirmware *self = FU_IFWI_CPD_FIRMWARE(firmware);
	FuIfwiCpdFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "header_version", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->header_version = val;
	}
	tmp = xb_node_query_text(n, "entry_version", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->entry_version = val;
	}

	/* success */
	return TRUE;
}

static void
fu_ifwi_cpd_firmware_init(FuIfwiCpdFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX);
}

static void
fu_ifwi_cpd_firmware_class_init(FuIfwiCpdFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_ifwi_cpd_firmware_validate;
	firmware_class->export = fu_ifwi_cpd_firmware_export;
	firmware_class->parse = fu_ifwi_cpd_firmware_parse;
	firmware_class->write = fu_ifwi_cpd_firmware_write;
	firmware_class->build = fu_ifwi_cpd_firmware_build;
}

/**
 * fu_ifwi_cpd_firmware_new:
 *
 * Creates a new #FuFirmware of Intel Code Partition Directory format
 *
 * Since: 1.8.2
 **/
FuFirmware *
fu_ifwi_cpd_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IFWI_CPD_FIRMWARE, NULL));
}
