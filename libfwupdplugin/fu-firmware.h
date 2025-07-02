/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>
#include <xmlb.h>

#include "fu-chunk.h"

#define FU_TYPE_FIRMWARE (fu_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuFirmware, fu_firmware, FU, FIRMWARE, GObject)

/**
 * FuFirmwareExportFlags:
 *
 * The firmware export flags.
 **/
typedef enum {
	/**
	 * FU_FIRMWARE_EXPORT_FLAG_NONE:
	 *
	 * No flags set.
	 *
	 * Since: 1.6.0
	 **/
	FU_FIRMWARE_EXPORT_FLAG_NONE = 0u,
	/**
	 * FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG:
	 *
	 * Include debug information when exporting.
	 *
	 * Since: 1.6.0
	 **/
	FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG = 1u << 0,
	/**
	 * FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA:
	 *
	 * Write the data as UTF-8 strings.
	 *
	 * Since: 1.6.0
	 **/
	FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA = 1u << 1,
	/**
	 * FU_FIRMWARE_EXPORT_FLAG_UNKNOWN:
	 *
	 * Unknown flag value.
	 *
	 * Since: 2.0.0
	 */
	FU_FIRMWARE_EXPORT_FLAG_UNKNOWN = G_MAXUINT64,
} FuFirmwareExportFlags;

/**
 * FuFirmwareParseFlags:
 *
 * The firmware parse flags.
 **/
typedef enum {
	/**
	 * FWUPD_INSTALL_FLAG_NONE:
	 *
	 * No flags set.
	 *
	 * Since: 2.0.9
	 */
	FU_FIRMWARE_PARSE_FLAG_NONE = 0,
	/**
	 * FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM:
	 *
	 * Ignore firmware CRCs and checksums.
	 *
	 * Since: 2.0.9
	 */
	FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM = 1 << 6,
	/**
	 * FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID:
	 *
	 * Ignore firmware vendor and project checks.
	 *
	 * Since: 2.0.9
	 */
	FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID = 1 << 7,
	/**
	 * FU_FIRMWARE_PARSE_FLAG_NO_SEARCH:
	 *
	 * Do not use heuristics when parsing the image.
	 *
	 * Since: 2.0.9
	 */
	FU_FIRMWARE_PARSE_FLAG_NO_SEARCH = 1 << 8,
	/**
	 * FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM:
	 *
	 * Keep a reference to the parsed stream.
	 *
	 * Since: 2.0.9
	 */
	FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM = 1 << 10,
	/**
	 * FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB:
	 *
	 * Keep a reference to the parsed blob in-memory.
	 *
	 * This allows the stream to be closed even when the firmware needs to be re-parsed.
	 *
	 * Since: 2.0.9
	 */
	FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB = 1 << 11,
} FuFirmwareParseFlags;

struct _FuFirmwareClass {
	GObjectClass parent_class;
	gboolean (*parse)(FuFirmware *self,
			  GInputStream *stream,
			  FuFirmwareParseFlags flags,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;
	GByteArray *(*write)(FuFirmware *self, GError **error)G_GNUC_WARN_UNUSED_RESULT;
	void (*export)(FuFirmware *self, FuFirmwareExportFlags flags, XbBuilderNode *bn);
	gboolean (*tokenize)(FuFirmware *self,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*build)(FuFirmware *self, XbNode *n, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gchar *(*get_checksum)(FuFirmware *self,
			       GChecksumType csum_kind,
			       GError **error)G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*validate)(FuFirmware *self, GInputStream *stream, gsize offset, GError **error);
	gboolean (*check_compatible)(FuFirmware *self,
				     FuFirmware *other,
				     FuFirmwareParseFlags flags,
				     GError **error);
	gchar *(*convert_version)(FuFirmware *self, guint64 version_raw);
};

/**
 * FuFirmwareFlags:
 *
 * The firmware flags.
 **/
typedef enum {
	/**
	 * FU_FIRMWARE_FLAG_NONE:
	 *
	 * No flags set.
	 *
	 * Since: 1.5.0
	 **/
	FU_FIRMWARE_FLAG_NONE = 0u,
	/**
	 * FU_FIRMWARE_FLAG_DEDUPE_ID:
	 *
	 * Dedupe images by ID.
	 *
	 * Since: 1.5.0
	 **/
	FU_FIRMWARE_FLAG_DEDUPE_ID = 1u << 0,
	/**
	 * FU_FIRMWARE_FLAG_DEDUPE_IDX:
	 *
	 * Dedupe images by IDX.
	 *
	 * Since: 1.5.0
	 **/
	FU_FIRMWARE_FLAG_DEDUPE_IDX = 1u << 1,
	/**
	 * FU_FIRMWARE_FLAG_HAS_CHECKSUM:
	 *
	 * Has a CRC or checksum to test internal consistency.
	 *
	 * Since: 1.5.6
	 **/
	FU_FIRMWARE_FLAG_HAS_CHECKSUM = 1u << 2,
	/**
	 * FU_FIRMWARE_FLAG_HAS_VID_PID:
	 *
	 * Has a vendor or product ID in the firmware.
	 *
	 * Since: 1.5.6
	 **/
	FU_FIRMWARE_FLAG_HAS_VID_PID = 1u << 3,
	/**
	 * FU_FIRMWARE_FLAG_DONE_PARSE:
	 *
	 * The firmware object has been used by fu_firmware_parse_bytes().
	 *
	 * Since: 1.7.3
	 **/
	FU_FIRMWARE_FLAG_DONE_PARSE = 1u << 4,
	/**
	 * FU_FIRMWARE_FLAG_HAS_STORED_SIZE:
	 *
	 * Encodes the image size in the firmware.
	 *
	 * Since: 1.8.2
	 **/
	FU_FIRMWARE_FLAG_HAS_STORED_SIZE = 1u << 5,
	/**
	 * FU_FIRMWARE_FLAG_ALWAYS_SEARCH:
	 *
	 * Always searches for magic regardless of the install flags.
	 * This is useful for firmware that always has an *unparsed* variable-length
	 * header.
	 *
	 * Since: 1.8.6
	 **/
	FU_FIRMWARE_FLAG_ALWAYS_SEARCH = 1u << 6,
	/**
	 * FU_FIRMWARE_FLAG_NO_AUTO_DETECTION:
	 *
	 * Do not use this firmware type when auto-detecting firmware.
	 * This should be used when there is no valid signature or CRC to check validity when
	 *parsing.
	 *
	 * Since: 1.9.3
	 **/
	FU_FIRMWARE_FLAG_NO_AUTO_DETECTION = 1u << 7,
	/**
	 * FU_FIRMWARE_FLAG_HAS_CHECK_COMPATIBLE:
	 *
	 * The firmware subclass implements a compatibility check.
	 *
	 * Since: 1.9.20
	 **/
	FU_FIRMWARE_FLAG_HAS_CHECK_COMPATIBLE = 1u << 8,
	/**
	 * FU_FIRMWARE_FLAG_UNKNOWN:
	 *
	 * Unknown flag value.
	 *
	 * Since: 2.0.0
	 */
	FU_FIRMWARE_FLAG_UNKNOWN = G_MAXUINT64,
} FuFirmwareFlags;

/**
 * FU_FIRMWARE_ID_PAYLOAD:
 *
 * The usual firmware ID string for the payload.
 *
 * Since: 1.6.0
 **/
#define FU_FIRMWARE_ID_PAYLOAD "payload"
/**
 * FU_FIRMWARE_ID_SIGNATURE:
 *
 * The usual firmware ID string for the signature.
 *
 * Since: 1.6.0
 **/
#define FU_FIRMWARE_ID_SIGNATURE "signature"
/**
 * FU_FIRMWARE_ID_HEADER:
 *
 * The usual firmware ID string for the header.
 *
 * Since: 1.6.0
 **/
#define FU_FIRMWARE_ID_HEADER "header"

/**
 * FuFirmwareAlignment:
 *
 * The firmware alignment position.
 **/
typedef enum {
	FU_FIRMWARE_ALIGNMENT_1 = 0x00,
	FU_FIRMWARE_ALIGNMENT_2 = 0x01,
	FU_FIRMWARE_ALIGNMENT_4 = 0x02,
	FU_FIRMWARE_ALIGNMENT_8 = 0x03,
	FU_FIRMWARE_ALIGNMENT_16 = 0x04,
	FU_FIRMWARE_ALIGNMENT_32 = 0x05,
	FU_FIRMWARE_ALIGNMENT_64 = 0x06,
	FU_FIRMWARE_ALIGNMENT_128 = 0x07,
	FU_FIRMWARE_ALIGNMENT_256 = 0x08,
	FU_FIRMWARE_ALIGNMENT_512 = 0x09,
	FU_FIRMWARE_ALIGNMENT_1K = 0x0A,
	FU_FIRMWARE_ALIGNMENT_2K = 0x0B,
	FU_FIRMWARE_ALIGNMENT_4K = 0x0C,
	FU_FIRMWARE_ALIGNMENT_8K = 0x0D,
	FU_FIRMWARE_ALIGNMENT_16K = 0x0E,
	FU_FIRMWARE_ALIGNMENT_32K = 0x0F,
	FU_FIRMWARE_ALIGNMENT_64K = 0x10,
	FU_FIRMWARE_ALIGNMENT_128K = 0x11,
	FU_FIRMWARE_ALIGNMENT_256K = 0x12,
	FU_FIRMWARE_ALIGNMENT_512K = 0x13,
	FU_FIRMWARE_ALIGNMENT_1M = 0x14,
	FU_FIRMWARE_ALIGNMENT_2M = 0x15,
	FU_FIRMWARE_ALIGNMENT_4M = 0x16,
	FU_FIRMWARE_ALIGNMENT_8M = 0x17,
	FU_FIRMWARE_ALIGNMENT_16M = 0x18,
	FU_FIRMWARE_ALIGNMENT_32M = 0x19,
	FU_FIRMWARE_ALIGNMENT_64M = 0x1A,
	FU_FIRMWARE_ALIGNMENT_128M = 0x1B,
	FU_FIRMWARE_ALIGNMENT_256M = 0x1C,
	FU_FIRMWARE_ALIGNMENT_512M = 0x1D,
	FU_FIRMWARE_ALIGNMENT_1G = 0x1E,
	FU_FIRMWARE_ALIGNMENT_2G = 0x1F,
	FU_FIRMWARE_ALIGNMENT_4G = 0x20,
	FU_FIRMWARE_ALIGNMENT_LAST,
} FuFirmwareAlignment;

#define FU_FIRMWARE_SEARCH_MAGIC_BUFSZ_MAX (32 * 1024 * 1024)

const gchar *
fu_firmware_flag_to_string(FuFirmwareFlags flag);
FuFirmwareFlags
fu_firmware_flag_from_string(const gchar *flag);

FuFirmware *
fu_firmware_new(void);
FuFirmware *
fu_firmware_new_from_bytes(GBytes *fw);
FuFirmware *
fu_firmware_new_from_gtypes(GInputStream *stream,
			    gsize offset,
			    FuFirmwareParseFlags flags,
			    GError **error,
			    ...) G_GNUC_NON_NULL(1);
gchar *
fu_firmware_to_string(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_export(FuFirmware *self, FuFirmwareExportFlags flags, XbBuilderNode *bn)
    G_GNUC_NON_NULL(1, 3);
gchar *
fu_firmware_export_to_xml(FuFirmware *self, FuFirmwareExportFlags flags, GError **error)
    G_GNUC_NON_NULL(1);
const gchar *
fu_firmware_get_version(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_version(FuFirmware *self, const gchar *version) G_GNUC_NON_NULL(1);
guint64
fu_firmware_get_version_raw(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_version_raw(FuFirmware *self, guint64 version_raw) G_GNUC_NON_NULL(1);
void
fu_firmware_set_version_format(FuFirmware *self, FwupdVersionFormat version_format)
    G_GNUC_NON_NULL(1);
FwupdVersionFormat
fu_firmware_get_version_format(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_add_flag(FuFirmware *firmware, FuFirmwareFlags flag) G_GNUC_NON_NULL(1);
gboolean
fu_firmware_has_flag(FuFirmware *firmware, FuFirmwareFlags flag) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
const gchar *
fu_firmware_get_filename(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_filename(FuFirmware *self, const gchar *filename) G_GNUC_NON_NULL(1);
const gchar *
fu_firmware_get_id(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_id(FuFirmware *self, const gchar *id) G_GNUC_NON_NULL(1);
guint64
fu_firmware_get_addr(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_addr(FuFirmware *self, guint64 addr) G_GNUC_NON_NULL(1);
guint64
fu_firmware_get_offset(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_offset(FuFirmware *self, guint64 offset) G_GNUC_NON_NULL(1);
gsize
fu_firmware_get_size(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_size(FuFirmware *self, gsize size) G_GNUC_NON_NULL(1);
void
fu_firmware_set_size_max(FuFirmware *self, gsize size_max) G_GNUC_NON_NULL(1);
gsize
fu_firmware_get_size_max(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_images_max(FuFirmware *self, guint images_max) G_GNUC_NON_NULL(1);
guint
fu_firmware_get_images_max(FuFirmware *self) G_GNUC_NON_NULL(1);
guint
fu_firmware_get_depth(FuFirmware *self) G_GNUC_NON_NULL(1);
guint64
fu_firmware_get_idx(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_idx(FuFirmware *self, guint64 idx) G_GNUC_NON_NULL(1);
GBytes *
fu_firmware_get_bytes(FuFirmware *self, GError **error) G_GNUC_NON_NULL(1);
GBytes *
fu_firmware_get_bytes_with_patches(FuFirmware *self, GError **error) G_GNUC_NON_NULL(1);
void
fu_firmware_set_bytes(FuFirmware *self, GBytes *bytes) G_GNUC_NON_NULL(1);
gboolean
fu_firmware_set_stream(FuFirmware *self, GInputStream *stream, GError **error) G_GNUC_NON_NULL(1);
GInputStream *
fu_firmware_get_stream(FuFirmware *self, GError **error) G_GNUC_NON_NULL(1);
FuFirmwareAlignment
fu_firmware_get_alignment(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_alignment(FuFirmware *self, FuFirmwareAlignment alignment) G_GNUC_NON_NULL(1);
void
fu_firmware_add_chunk(FuFirmware *self, FuChunk *chk) G_GNUC_NON_NULL(1);
GPtrArray *
fu_firmware_get_chunks(FuFirmware *self, GError **error) G_GNUC_NON_NULL(1);
FuFirmware *
fu_firmware_get_parent(FuFirmware *self) G_GNUC_NON_NULL(1);
void
fu_firmware_set_parent(FuFirmware *self, FuFirmware *parent) G_GNUC_NON_NULL(1);

gboolean
fu_firmware_tokenize(FuFirmware *self,
		     GInputStream *stream,
		     FuFirmwareParseFlags flags,
		     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_firmware_build(FuFirmware *self, XbNode *n, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_firmware_build_from_xml(FuFirmware *self,
			   const gchar *xml,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_firmware_build_from_filename(FuFirmware *self,
				const gchar *filename,
				GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_firmware_parse_stream(FuFirmware *self,
			 GInputStream *stream,
			 gsize offset,
			 FuFirmwareParseFlags flags,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_firmware_parse_file(FuFirmware *self, GFile *file, FuFirmwareParseFlags flags, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_firmware_parse_bytes(FuFirmware *self,
			GBytes *fw,
			gsize offset,
			FuFirmwareParseFlags flags,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
GBytes *
fu_firmware_write(FuFirmware *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
GBytes *
fu_firmware_write_chunk(FuFirmware *self, guint64 address, guint64 chunk_sz_max, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_firmware_write_file(FuFirmware *self, GFile *file, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gchar *
fu_firmware_get_checksum(FuFirmware *self, GChecksumType csum_kind, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_firmware_check_compatible(FuFirmware *self,
			     FuFirmware *other,
			     FuFirmwareParseFlags flags,
			     GError **error) G_GNUC_NON_NULL(1, 2);

void
fu_firmware_add_image(FuFirmware *self, FuFirmware *img) G_GNUC_NON_NULL(1, 2);
gboolean
fu_firmware_add_image_full(FuFirmware *self, FuFirmware *img, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_firmware_remove_image(FuFirmware *self, FuFirmware *img, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_firmware_remove_image_by_idx(FuFirmware *self, guint64 idx, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_firmware_remove_image_by_id(FuFirmware *self, const gchar *id, GError **error)
    G_GNUC_NON_NULL(1);
GPtrArray *
fu_firmware_get_images(FuFirmware *self) G_GNUC_NON_NULL(1);
FuFirmware *
fu_firmware_get_image_by_id(FuFirmware *self, const gchar *id, GError **error) G_GNUC_NON_NULL(1);
GBytes *
fu_firmware_get_image_by_id_bytes(FuFirmware *self, const gchar *id, GError **error)
    G_GNUC_NON_NULL(1);
GInputStream *
fu_firmware_get_image_by_id_stream(FuFirmware *self, const gchar *id, GError **error)
    G_GNUC_NON_NULL(1);
FuFirmware *
fu_firmware_get_image_by_idx(FuFirmware *self, guint64 idx, GError **error) G_GNUC_NON_NULL(1);
GBytes *
fu_firmware_get_image_by_idx_bytes(FuFirmware *self, guint64 idx, GError **error)
    G_GNUC_NON_NULL(1);
GInputStream *
fu_firmware_get_image_by_idx_stream(FuFirmware *self, guint64 idx, GError **error)
    G_GNUC_NON_NULL(1);
FuFirmware *
fu_firmware_get_image_by_gtype(FuFirmware *self, GType gtype, GError **error) G_GNUC_NON_NULL(1);
GBytes *
fu_firmware_get_image_by_gtype_bytes(FuFirmware *self, GType gtype, GError **error)
    G_GNUC_NON_NULL(1);
FuFirmware *
fu_firmware_get_image_by_checksum(FuFirmware *self, const gchar *checksum, GError **error)
    G_GNUC_NON_NULL(1, 2);
void
fu_firmware_add_patch(FuFirmware *self, gsize offset, GBytes *blob) G_GNUC_NON_NULL(1, 3);
