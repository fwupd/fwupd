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
#include "fu-string.h"
#include "fu-struct.h"

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

#define FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX   1024

static void
fu_ifwi_cpd_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIfwiCpdFirmware *self = FU_IFWI_CPD_FIRMWARE(firmware);
	FuIfwiCpdFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "header_version", priv->header_version);
	fu_xmlb_builder_insert_kx(bn, "entry_version", priv->entry_version);
}

static gboolean
fu_ifwi_cpd_firmware_parse_manifest(FuFirmware *firmware, GBytes *fw, GError **error)
{
	FuStruct *st_mex = fu_struct_lookup(firmware, "IfwiCpdManifestExtHeader");
	FuStruct *st_mhd = fu_struct_lookup(firmware, "IfwiCpdManifestHeader");
	gsize bufsz = 0;
	guint32 size;
	gsize offset = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* raw version */
	if (!fu_struct_unpack_full(st_mhd, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
		return FALSE;
	fu_firmware_set_version_raw(firmware, fu_struct_get_u64(st_mhd, "version"));

	/* verify the size */
	size = fu_struct_get_u32(st_mhd, "size");
	if (size * 4 != bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid manifest invalid length, got 0x%x, expected 0x%x",
			    size * 4,
			    (guint)bufsz);
		return FALSE;
	}

	/* parse extensions */
	offset += fu_struct_get_u32(st_mhd, "header_length") * 4;
	while (offset < bufsz) {
		guint32 extension_type = 0;
		guint32 extension_length = 0;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();

		/* set the extension type as the index */
		if (!fu_struct_unpack_full(st_mex, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
			return FALSE;
		extension_type = fu_struct_get_u32(st_mex, "extension_type");
		if (extension_type == 0x0)
			break;
		fu_firmware_set_idx(img, extension_type);

		/* add data section */
		extension_length = fu_struct_get_u32(st_mex, "extension_length");
		if (extension_length == 0x0)
			break;
		if (extension_length < fu_struct_size(st_mex)) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid manifest extension header length 0x%x",
				    (guint)extension_length);
			return FALSE;
		}
		blob = fu_bytes_new_offset(fw,
					   offset + fu_struct_size(st_mex),
					   extension_length - fu_struct_size(st_mex),
					   error);
		if (blob == NULL)
			return FALSE;
		fu_firmware_set_bytes(img, blob);

		/* success */
		fu_firmware_set_offset(img, offset);
		fu_firmware_add_image(firmware, img);
		offset += extension_length;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ifwi_cpd_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	FuStruct *st_hdr = fu_struct_lookup(firmware, "IfwiCpdHeader");
	return fu_struct_unpack_full(st_hdr,
				     g_bytes_get_data(fw, NULL),
				     g_bytes_get_size(fw),
				     offset,
				     FU_STRUCT_FLAG_ONLY_CONSTANTS,
				     error);
}

static gboolean
fu_ifwi_cpd_firmware_parse(FuFirmware *firmware,
			   GBytes *fw,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuIfwiCpdFirmware *self = FU_IFWI_CPD_FIRMWARE(firmware);
	FuIfwiCpdFirmwarePrivate *priv = GET_PRIVATE(self);
	FuStruct *st_hdr = fu_struct_lookup(self, "IfwiCpdHeader");
	FuStruct *st_mhd = fu_struct_lookup(self, "IfwiCpdManifestHeader");
	FuStruct *st_ent = fu_struct_lookup(self, "IfwiCpdEntry");
	gsize bufsz = 0;
	guint32 num_of_entries;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* other header fields */
	if (!fu_struct_unpack_full(st_hdr, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
		return FALSE;
	priv->header_version = fu_struct_get_u8(st_hdr, "header_version");
	priv->entry_version = fu_struct_get_u8(st_hdr, "entry_version");
	fu_firmware_set_idx(firmware, fu_struct_get_u32(st_hdr, "partition_name"));

	/* read out entries */
	num_of_entries = fu_struct_get_u32(st_hdr, "num_of_entries");
	if (num_of_entries > FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "too many entries 0x%x, expected <= 0x%x",
			    num_of_entries,
			    (guint)FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX);
		return FALSE;
	}
	offset += fu_struct_get_u8(st_hdr, "header_length");
	for (guint32 i = 0; i < num_of_entries; i++) {
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(GBytes) img_blob = NULL;
		g_autofree gchar *id = NULL;
		guint32 img_offset = 0;

		/* the IDX is the position in the file */
		fu_firmware_set_idx(img, i);

		if (!fu_struct_unpack_full(st_ent, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
			return FALSE;

		/* copy name as id */
		id = fu_struct_get_string(st_ent, "name");
		fu_firmware_set_id(img, id);

		/* copy offset, ignoring huffman and reserved bits */
		img_offset = fu_struct_get_u32(st_ent, "offset");
		img_offset &= 0x1FFFFFF;
		fu_firmware_set_offset(img, img_offset);

		/* copy data */
		img_blob =
		    fu_bytes_new_offset(fw, img_offset, fu_struct_get_u32(st_ent, "length"), error);
		if (img_blob == NULL)
			return FALSE;
		fu_firmware_set_bytes(img, img_blob);

		/* read the manifest */
		if (i == FU_IFWI_CPD_FIRMWARE_IDX_MANIFEST &&
		    g_bytes_get_size(img_blob) > fu_struct_size(st_mhd)) {
			if (!fu_ifwi_cpd_firmware_parse_manifest(img, img_blob, error))
				return FALSE;
		}

		/* success */
		fu_firmware_add_image(firmware, img);
		offset += fu_struct_size(st_ent);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_ifwi_cpd_firmware_write(FuFirmware *firmware, GError **error)
{
	FuIfwiCpdFirmware *self = FU_IFWI_CPD_FIRMWARE(firmware);
	FuIfwiCpdFirmwarePrivate *priv = GET_PRIVATE(self);
	FuStruct *st_hdr = fu_struct_lookup(self, "IfwiCpdHeader");
	FuStruct *st_ent = fu_struct_lookup(self, "IfwiCpdEntry");
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	gsize offset = 0;

	/* write the header */
	fu_struct_set_u32(st_hdr, "num_of_entries", imgs->len);
	fu_struct_set_u8(st_hdr, "header_version", priv->header_version);
	fu_struct_set_u8(st_hdr, "entry_version", priv->entry_version);
	fu_struct_set_u8(st_hdr, "checksum", 0x0);
	fu_struct_set_u32(st_hdr, "partition_name", fu_firmware_get_idx(firmware));
	fu_struct_set_u32(st_hdr, "crc32", 0x0);
	buf = fu_struct_pack(st_hdr);

	/* fixup the image offsets */
	offset += fu_struct_size(st_hdr);
	offset += fu_struct_size(st_ent) * imgs->len;
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

		/* sanity check */
		if (fu_firmware_get_id(img) == NULL) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "image 0x%x must have an ID",
				    (guint)fu_firmware_get_idx(img));
			return NULL;
		}
		if (!fu_struct_set_string(st_ent, "name", fu_firmware_get_id(img), error))
			return NULL;
		fu_struct_set_u32(st_ent, "offset", fu_firmware_get_offset(img));
		fu_struct_set_u32(st_ent, "length", fu_firmware_get_size(img));
		fu_struct_pack_into(st_ent, buf);
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
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
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
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT8, error))
			return FALSE;
		priv->header_version = val;
	}
	tmp = xb_node_query_text(n, "entry_version", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT8, error))
			return FALSE;
		priv->entry_version = val;
	}

	/* success */
	return TRUE;
}

static void
fu_ifwi_cpd_firmware_init(FuIfwiCpdFirmware *self)
{
	fu_struct_register(self,
			   "IfwiCpdHeader {"
			   "    header_marker: u32le:: 0x44504324,"
			   "    num_of_entries: u32le,"
			   "    header_version: u8,"
			   "    entry_version: u8,"
			   "    header_length: u8: $struct_size,"
			   "    checksum: u8,"
			   "    partition_name: u32le,"
			   "    crc32: u32le,"
			   "}");
	fu_struct_register(self,
			   "IfwiCpdEntry {"
			   "    name: 12s,"
			   "    offset: u32le,"
			   "    length: u32le,"
			   "    reserved1: 4u8,"
			   "}");
	fu_struct_register(self,
			   "IfwiCpdManifestHeader {"
			   "    header_type: u32le,"
			   "    header_length: u32le," /* dwords */
			   "    header_version: u32le,"
			   "    flags: u32le,"
			   "    vendor: u32le,"
			   "    date: u32le,"
			   "    size: u32le," /* dwords */
			   "    id: u32le,"
			   "    rsvd: u32le,"
			   "    version: u64le,"
			   "    svn: u32le,"
			   "}");
	fu_struct_register(self,
			   "IfwiCpdManifestExtHeader {"
			   "    extension_type: u32le,"
			   "    extension_length: u32le,"
			   "}");
}

static void
fu_ifwi_cpd_firmware_class_init(FuIfwiCpdFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_ifwi_cpd_firmware_check_magic;
	klass_firmware->export = fu_ifwi_cpd_firmware_export;
	klass_firmware->parse = fu_ifwi_cpd_firmware_parse;
	klass_firmware->write = fu_ifwi_cpd_firmware_write;
	klass_firmware->build = fu_ifwi_cpd_firmware_build;
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
