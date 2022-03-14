/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#if HAVE_CBOR
#include <cbor.h>
#endif

#include "fu-common.h"
#include "fu-coswid-firmware.h"

/**
 * FuCoswidFirmware:
 *
 * A coSWID SWID section.
 *
 * See also: [class@FuUswidFirmware]
 */

G_DEFINE_TYPE(FuCoswidFirmware, fu_coswid_firmware, FU_TYPE_FIRMWARE)

#define COSWID_GLOBAL_MAP_TAG_ID		    0
#define COSWID_GLOBAL_MAP_SOFTWARE_NAME		    1
#define COSWID_GLOBAL_MAP_ENTITY		    2
#define COSWID_GLOBAL_MAP_EVIDENCE		    3
#define COSWID_GLOBAL_MAP_LINK			    4
#define COSWID_GLOBAL_MAP_SOFTWARE_META		    5
#define COSWID_GLOBAL_MAP_PAYLOAD		    6
#define COSWID_GLOBAL_MAP_HASH			    7
#define COSWID_GLOBAL_MAP_CORPUS		    8
#define COSWID_GLOBAL_MAP_PATCH			    9
#define COSWID_GLOBAL_MAP_MEDIA			    10
#define COSWID_GLOBAL_MAP_SUPPLEMENTAL		    11
#define COSWID_GLOBAL_MAP_TAG_VERSION		    12
#define COSWID_GLOBAL_MAP_SOFTWARE_VERSION	    13
#define COSWID_GLOBAL_MAP_VERSION_SCHEME	    14
#define COSWID_GLOBAL_MAP_LANG			    15
#define COSWID_GLOBAL_MAP_DIRECTORY		    16
#define COSWID_GLOBAL_MAP_FILE			    17
#define COSWID_GLOBAL_MAP_PROCESS		    18
#define COSWID_GLOBAL_MAP_RESOURCE		    19
#define COSWID_GLOBAL_MAP_SIZE			    20
#define COSWID_GLOBAL_MAP_FILE_VERSION		    21
#define COSWID_GLOBAL_MAP_KEY			    22
#define COSWID_GLOBAL_MAP_LOCATION		    23
#define COSWID_GLOBAL_MAP_FS_NAME		    24
#define COSWID_GLOBAL_MAP_ROOT			    25
#define COSWID_GLOBAL_MAP_PATH_ELEMENTS		    26
#define COSWID_GLOBAL_MAP_PROCESS_NAME		    27
#define COSWID_GLOBAL_MAP_PID			    28
#define COSWID_GLOBAL_MAP_TYPE			    29
#define COSWID_GLOBAL_MAP_ENTITY_NAME		    31
#define COSWID_GLOBAL_MAP_REG_ID		    32
#define COSWID_GLOBAL_MAP_ROLE			    33
#define COSWID_GLOBAL_MAP_THUMBPRINT		    34
#define COSWID_GLOBAL_MAP_DATE			    35
#define COSWID_GLOBAL_MAP_DEVICE_ID		    36
#define COSWID_GLOBAL_MAP_ARTIFACT		    37
#define COSWID_GLOBAL_MAP_HREF			    38
#define COSWID_GLOBAL_MAP_OWNERSHIP		    39
#define COSWID_GLOBAL_MAP_REL			    40
#define COSWID_GLOBAL_MAP_MEDIA_TYPE		    41
#define COSWID_GLOBAL_MAP_USE			    42
#define COSWID_GLOBAL_MAP_ACTIVATION_STATUS	    43
#define COSWID_GLOBAL_MAP_CHANNEL_TYPE		    44
#define COSWID_GLOBAL_MAP_COLLOQUIAL_VERSION	    45
#define COSWID_GLOBAL_MAP_DESCRIPTION		    46
#define COSWID_GLOBAL_MAP_EDITION		    47
#define COSWID_GLOBAL_MAP_ENTITLEMENT_DATA_REQUIRED 48
#define COSWID_GLOBAL_MAP_ENTITLEMENT_KEY	    49
#define COSWID_GLOBAL_MAP_GENERATOR		    50
#define COSWID_GLOBAL_MAP_PERSISTENT_ID		    51
#define COSWID_GLOBAL_MAP_PRODUCT		    52
#define COSWID_GLOBAL_MAP_PRODUCT_FAMILY	    53
#define COSWID_GLOBAL_MAP_REVISION		    54
#define COSWID_GLOBAL_MAP_SUMMARY		    55
#define COSWID_GLOBAL_MAP_UNSPSC_CODE		    56
#define COSWID_GLOBAL_MAP_UNSPSC_VERSION	    57

#if HAVE_CBOR
G_DEFINE_AUTOPTR_CLEANUP_FUNC(cbor_item_t, cbor_intermediate_decref)

static gchar *
fu_coswid_firmware_strndup(cbor_item_t *item)
{
	if (!cbor_string_is_definite(item))
		return NULL;
	return g_strndup((const gchar *)cbor_string_handle(item), cbor_string_length(item));
}
#endif

static gboolean
fu_coswid_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 guint64 addr_start,
			 guint64 addr_end,
			 FwupdInstallFlags flags,
			 GError **error)
{
#if HAVE_CBOR
	struct cbor_load_result result = {0x0};
	struct cbor_pair *pairs = NULL;
	g_autoptr(cbor_item_t) item = NULL;

	item = cbor_load(g_bytes_get_data(fw, NULL), g_bytes_get_size(fw), &result);
	if (item == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to parse CBOR at offset 0x%x: 0x%x",
			    (guint)result.error.position,
			    result.error.code);
		return FALSE;
	}
	if (result.read != g_bytes_get_size(fw)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "read 0x%x of payload 0x%x",
			    (guint)result.read,
			    (guint)g_bytes_get_size(fw));
		return FALSE;
	}

	/* pretty-print the result */
	if (g_getenv("FWUPD_CBOR_VERBOSE") != NULL) {
		cbor_describe(item, stdout);
		fflush(stdout);
	}

	/* parse out anything interesting */
	pairs = cbor_map_handle(item);
	for (gsize i = 0; i < cbor_map_size(item); i++) {
		guint8 tag_id = cbor_get_uint8(pairs[i].key);
		if (tag_id == COSWID_GLOBAL_MAP_TAG_ID) {
			g_autofree gchar *str = fu_coswid_firmware_strndup(pairs[i].value);
			fu_firmware_set_id(firmware, str);
		} else if (tag_id == COSWID_GLOBAL_MAP_SOFTWARE_NAME) {
			g_autofree gchar *str = fu_coswid_firmware_strndup(pairs[i].value);
			fu_firmware_set_filename(firmware, str);
		} else if (tag_id == COSWID_GLOBAL_MAP_SOFTWARE_VERSION) {
			g_autofree gchar *str = fu_coswid_firmware_strndup(pairs[i].value);
			fu_firmware_set_version(firmware, str);
		}
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "not compiled with CBOR support");
	return FALSE;
#endif
}

static GBytes *
fu_coswid_firmware_write(FuFirmware *firmware, GError **error)
{
#if HAVE_CBOR
	gsize buflen;
	gsize bufsz = 0;
	g_autofree guchar *buf = NULL;
	g_autoptr(cbor_item_t) root = cbor_new_indefinite_map();

	/* preallocate the map structure */
	if (fu_firmware_get_id(firmware) != NULL) {
		g_autoptr(cbor_item_t) key = cbor_build_uint8(COSWID_GLOBAL_MAP_TAG_ID);
		g_autoptr(cbor_item_t) val = cbor_build_string(fu_firmware_get_id(firmware));
		cbor_map_add(root, (struct cbor_pair){.key = key, .value = val});
	}
	if (fu_firmware_get_version(firmware) != NULL) {
		g_autoptr(cbor_item_t) key = cbor_build_uint8(COSWID_GLOBAL_MAP_SOFTWARE_VERSION);
		g_autoptr(cbor_item_t) val = cbor_build_string(fu_firmware_get_version(firmware));
		cbor_map_add(root, (struct cbor_pair){.key = key, .value = val});
	}
	if (fu_firmware_get_filename(firmware) != NULL) {
		g_autoptr(cbor_item_t) key = cbor_build_uint8(COSWID_GLOBAL_MAP_SOFTWARE_NAME);
		g_autoptr(cbor_item_t) val = cbor_build_string(fu_firmware_get_filename(firmware));
		cbor_map_add(root, (struct cbor_pair){.key = key, .value = val});
	}
	buflen = cbor_serialize_alloc(root, &buf, &bufsz);
	if (buflen > bufsz) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "CBOR allocation failure");
		return NULL;
	}
	return g_bytes_new(buf, buflen);
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "not compiled with CBOR support");
	return NULL;
#endif
}

static void
fu_coswid_firmware_init(FuCoswidFirmware *self)
{
}

static void
fu_coswid_firmware_class_init(FuCoswidFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_coswid_firmware_parse;
	klass_firmware->write = fu_coswid_firmware_write;
}

/**
 * fu_coswid_firmware_new:
 *
 * Creates a new #FuFirmware of sub type coSWID
 *
 * Since: 1.8.0
 **/
FuFirmware *
fu_coswid_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_COSWID_FIRMWARE, NULL));
}
