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

#define FU_IFWI_CPD_FIRMWARE_HEADER_MARKER 0x44504324
#define FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX   1024

typedef struct __attribute__((packed)) {
	guint32 header_marker;
	guint32 num_of_entries;
	guint8 header_version;
	guint8 entry_version;
	guint8 header_length;
	guint8 checksum;
	guint32 partition_name;
	guint32 crc32;
} FuIfwiCpdHeader;

typedef struct __attribute__((packed)) {
	gchar name[12];
	guint32 offset;
	guint32 length;
	guint8 reserved1[4];
} FuIfwiCpdEntry;

typedef struct __attribute__((packed)) {
	guint32 header_type;
	guint32 header_length; /* dwords */
	guint32 header_version;
	guint32 flags;
	guint32 vendor;
	guint32 date;
	guint32 size; /* dwords */
	guint32 id;
	guint32 rsvd;
	guint64 version;
	guint32 svn;
} FuIfwiCpdManifestHeader;

typedef struct __attribute__((packed)) {
	guint32 extension_type;
	guint32 extension_length;
} FuIfwiCpdManifestExtHeader;

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
	gsize bufsz = 0;
	guint32 header_length = 0;
	guint32 size = 0;
	guint64 version = 0;
	gsize offset = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* raw version */
	if (!fu_memread_uint64_safe(buf,
				    bufsz,
				    G_STRUCT_OFFSET(FuIfwiCpdManifestHeader, version),
				    &version,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	fu_firmware_set_version_raw(firmware, version);

	/* verify the size */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    G_STRUCT_OFFSET(FuIfwiCpdManifestHeader, size),
				    &size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
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
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    G_STRUCT_OFFSET(FuIfwiCpdManifestHeader, header_length),
				    &header_length,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	offset += header_length * 4;
	while (offset < bufsz) {
		guint32 extension_type = 0;
		guint32 extension_length = 0;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();

		/* set the extension type as the index */
		if (!fu_memread_uint32_safe(
			buf,
			bufsz,
			offset + G_STRUCT_OFFSET(FuIfwiCpdManifestExtHeader, extension_type),
			&extension_type,
			G_LITTLE_ENDIAN,
			error))
			return FALSE;
		if (extension_type == 0x0)
			break;
		fu_firmware_set_idx(img, extension_type);

		/* add data section */
		if (!fu_memread_uint32_safe(
			buf,
			bufsz,
			offset + G_STRUCT_OFFSET(FuIfwiCpdManifestExtHeader, extension_length),
			&extension_length,
			G_LITTLE_ENDIAN,
			error))
			return FALSE;
		if (extension_length == 0x0)
			break;
		if (extension_length < sizeof(FuIfwiCpdManifestExtHeader)) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid manifest extension header length 0x%x",
				    (guint)extension_length);
			return FALSE;
		}
		blob = fu_bytes_new_offset(fw,
					   offset + sizeof(FuIfwiCpdManifestExtHeader),
					   extension_length - sizeof(FuIfwiCpdManifestExtHeader),
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
	guint32 magic = 0;

	if (!fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset + G_STRUCT_OFFSET(FuIfwiCpdHeader, header_marker),
				    &magic,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (magic != FU_IFWI_CPD_FIRMWARE_HEADER_MARKER) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid CPD header marker 0x%x, expected 0x%x",
			    magic,
			    (guint)FU_IFWI_CPD_FIRMWARE_HEADER_MARKER);
		return FALSE;
	}

	/* success */
	return TRUE;
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
	gsize bufsz = 0;
	guint32 num_of_entries = 0;
	guint32 partition_name = 0;
	guint8 header_length = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* other header fields */
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   offset + G_STRUCT_OFFSET(FuIfwiCpdHeader, header_version),
				   &priv->header_version,
				   error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   offset + G_STRUCT_OFFSET(FuIfwiCpdHeader, entry_version),
				   &priv->entry_version,
				   error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   offset + G_STRUCT_OFFSET(FuIfwiCpdHeader, header_length),
				   &header_length,
				   error))
		return FALSE;
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuIfwiCpdHeader, partition_name),
				    &partition_name,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	fu_firmware_set_idx(firmware, partition_name);

	/* read out entries */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuIfwiCpdHeader, num_of_entries),
				    &num_of_entries,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (num_of_entries > FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "too many entries 0x%x, expected <= 0x%x",
			    num_of_entries,
			    (guint)FU_IFWI_CPD_FIRMWARE_ENTRIES_MAX);
		return FALSE;
	}
	offset += header_length;
	for (guint32 i = 0; i < num_of_entries; i++) {
		gchar name[12] = {0x0};
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(GBytes) img_blob = NULL;
		g_autofree gchar *id = NULL;
		guint32 img_offset = 0;
		guint32 img_length = 0;

		/* the IDX is the position in the file */
		fu_firmware_set_idx(img, i);

		/* copy name as id */
		if (!fu_memcpy_safe((guint8 *)name,
				    sizeof(name),
				    0x0, /* dst */
				    buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuIfwiCpdEntry, name), /* src */
				    sizeof(name),
				    error)) {
			return FALSE;
		}
		id = fu_strsafe(name, sizeof(name));
		fu_firmware_set_id(img, id);

		/* copy offset, ignoring huffman and reserved bits */
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset + G_STRUCT_OFFSET(FuIfwiCpdEntry, offset),
					    &img_offset,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		img_offset &= 0x1FFFFFF;
		fu_firmware_set_offset(img, img_offset);

		/* copy data */
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset + G_STRUCT_OFFSET(FuIfwiCpdEntry, length),
					    &img_length,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		img_blob = fu_bytes_new_offset(fw, img_offset, img_length, error);
		if (img_blob == NULL)
			return FALSE;
		fu_firmware_set_bytes(img, img_blob);

		/* read the manifest */
		if (i == FU_IFWI_CPD_FIRMWARE_IDX_MANIFEST &&
		    g_bytes_get_size(img_blob) > sizeof(FuIfwiCpdManifestHeader)) {
			if (!fu_ifwi_cpd_firmware_parse_manifest(img, img_blob, error))
				return FALSE;
		}

		/* success */
		fu_firmware_add_image(firmware, img);
		offset += sizeof(FuIfwiCpdEntry);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_ifwi_cpd_firmware_write(FuFirmware *firmware, GError **error)
{
	FuIfwiCpdFirmware *self = FU_IFWI_CPD_FIRMWARE(firmware);
	FuIfwiCpdFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	gsize offset = 0;

	/* write the header */
	fu_byte_array_append_uint32(buf, FU_IFWI_CPD_FIRMWARE_HEADER_MARKER, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, imgs->len, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(buf, priv->header_version);
	fu_byte_array_append_uint8(buf, priv->entry_version);
	fu_byte_array_append_uint8(buf, sizeof(FuIfwiCpdHeader));
	fu_byte_array_append_uint8(buf, 0x0); /* checksum */
	fu_byte_array_append_uint32(buf, fu_firmware_get_idx(firmware), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* crc32 */

	/* fixup the image offsets */
	offset += sizeof(FuIfwiCpdHeader);
	offset += sizeof(FuIfwiCpdEntry) * imgs->len;
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
		const gchar *id = fu_firmware_get_id(img);
		gchar name[12] = {0x0};

		/* sanity check */
		if (id == NULL) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "image 0x%x must have an ID",
				    (guint)fu_firmware_get_idx(img));
			return NULL;
		}

		/* copy id into name */
		if (!fu_memcpy_safe((guint8 *)name,
				    sizeof(name),
				    0x0, /* dst */
				    (const guint8 *)id,
				    strlen(id),
				    0x0, /* src */
				    strlen(id),
				    error)) {
			return NULL;
		}

		g_byte_array_append(buf, (const guint8 *)name, sizeof(name));
		fu_byte_array_append_uint32(buf, fu_firmware_get_offset(img), G_LITTLE_ENDIAN);
		fu_byte_array_append_uint32(buf, fu_firmware_get_size(img), G_LITTLE_ENDIAN);
		fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN); /* reserved */
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
