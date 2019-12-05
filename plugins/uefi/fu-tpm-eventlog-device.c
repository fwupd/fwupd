/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <tss2/tss2_esys.h>

#include "fu-common.h"

#include "fu-tpm-eventlog-common.h"
#include "fu-tpm-eventlog-device.h"

struct _FuTpmEventlogDevice {
	FuDevice		 parent_instance;
	GPtrArray		*items;
};

G_DEFINE_TYPE (FuTpmEventlogDevice, fu_tpm_eventlog_device, FU_TYPE_DEVICE)

#define FU_TPM_EVENTLOG_V1_IDX_PCR			0x00
#define FU_TPM_EVENTLOG_V1_IDX_TYPE			0x04
#define FU_TPM_EVENTLOG_V1_IDX_DIGEST			0x08
#define FU_TPM_EVENTLOG_V1_IDX_EVENT_SIZE		0x1c
#define FU_TPM_EVENTLOG_V1_SIZE				0x20

#define FU_TPM_EVENTLOG_V2_HDR_IDX_SIGNATURE		0x00
#define FU_TPM_EVENTLOG_V2_HDR_IDX_PLATFORM_CLASS	0x10
#define FU_TPM_EVENTLOG_V2_HDR_IDX_SPEC_VERSION_MINOR	0x14
#define FU_TPM_EVENTLOG_V2_HDR_IDX_SPEC_VERSION_MAJOR	0X15
#define FU_TPM_EVENTLOG_V2_HDR_IDX_SPEC_ERRATA		0x16
#define FU_TPM_EVENTLOG_V2_HDR_IDX_UINTN_SIZE		0x17
#define FU_TPM_EVENTLOG_V2_HDR_IDX_NUMBER_OF_ALGS	0x18

#define FU_TPM_EVENTLOG_V2_HDR_SIGNATURE		"Spec ID Event03"

#define FU_TPM_EVENTLOG_V2_IDX_PCR			0x00
#define FU_TPM_EVENTLOG_V2_IDX_TYPE			0x04
#define FU_TPM_EVENTLOG_V2_IDX_DIGEST_COUNT		0x08
#define FU_TPM_EVENTLOG_V2_SIZE				0x0c

typedef struct {
	FuTpmEventlogItemKind	 kind;
	gchar			*checksum;
	GBytes			*blob;
} FuTpmEventlogDeviceItem;

static gchar *
fu_tpm_eventlog_device_blobstr (GBytes *blob)
{
	gboolean has_printable = FALSE;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data (blob, &bufsz);
	g_autoptr(GString) str = g_string_new (NULL);

	for (gsize i = 0; i < bufsz; i++) {
		gchar chr = buf[i];
		if (g_ascii_isprint (chr)) {
			g_string_append_c (str, chr);
			has_printable = TRUE;
		} else {
			g_string_append_c (str, '.');
		}
	}
	if (!has_printable)
		return NULL;
	return g_string_free (g_steal_pointer (&str), FALSE);
}

static void
fu_tpm_eventlog_device_item_to_string (FuTpmEventlogDeviceItem *item, guint idt, GString *str)
{
	g_autofree gchar *blobstr = fu_tpm_eventlog_device_blobstr (item->blob);
	fu_common_string_append_kx (str, idt, "Type", item->kind);
	fu_common_string_append_kv (str, idt, "Description", fu_tpm_eventlog_item_kind_to_string (item->kind));
	fu_common_string_append_kv (str, idt, "Checksum", item->checksum);
	if (blobstr != NULL)
		fu_common_string_append_kv (str, idt, "BlobStr", blobstr);
}

static void
fu_tpm_eventlog_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuTpmEventlogDevice *self = FU_TPM_EVENTLOG_DEVICE (device);
	if (self->items->len > 0) {
		fu_common_string_append_kv (str, idt, "Items", NULL);
		for (guint i = 0; i < self->items->len; i++) {
			FuTpmEventlogDeviceItem *item = g_ptr_array_index (self->items, i);
			fu_tpm_eventlog_device_item_to_string (item, idt + 1, str);
		}
	}
}

gchar *
fu_tpm_eventlog_device_report_metadata (FuTpmEventlogDevice *self)
{
	GString *str = g_string_new ("");
	for (guint i = 0; i < self->items->len; i++) {
		FuTpmEventlogDeviceItem *item = g_ptr_array_index (self->items, i);
		g_autofree gchar *blobstr = fu_tpm_eventlog_device_blobstr (item->blob);
		g_string_append_printf (str, "0x%08x %s", item->kind, item->checksum);
		if (blobstr != NULL)
			g_string_append_printf (str, " [%s]", blobstr);
		g_string_append (str, "\n");
	}
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

static void
fu_tpm_eventlog_device_item_free (FuTpmEventlogDeviceItem *item)
{
	g_bytes_unref (item->blob);
	g_free (item->checksum);
	g_free (item);
}

static void
fu_tpm_eventlog_device_init (FuTpmEventlogDevice *self)
{
	self->items = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_tpm_eventlog_device_item_free);
	fu_device_set_name (FU_DEVICE (self), "Event Log");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_physical_id (FU_DEVICE (self), "DEVNAME=/dev/tpm0");
	fu_device_set_logical_id (FU_DEVICE (self), "eventlog");
	fu_device_add_parent_guid (FU_DEVICE (self), "main-system-firmware");
	fu_device_add_instance_id (FU_DEVICE (self), "system-tpm-eventlog");
}

static void
fu_tpm_eventlog_device_finalize (GObject *object)
{
	FuTpmEventlogDevice *self = FU_TPM_EVENTLOG_DEVICE (object);

	g_ptr_array_unref (self->items);

	G_OBJECT_CLASS (fu_tpm_eventlog_device_parent_class)->finalize (object);
}

static void
fu_tpm_eventlog_device_class_init (FuTpmEventlogDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_tpm_eventlog_device_finalize;
	klass_device->to_string = fu_tpm_eventlog_device_to_string;
}

static gchar *
fu_common_read_strhex_safe (const guint8 *buf,
			    gsize bufsz,
			    gsize offset,
			    gsize length,
			    GError **error)
{
	g_autoptr(GString) csum = g_string_new (NULL);
	g_autofree guint8 *digest = g_malloc0 (length);
	if (!fu_memcpy_safe (digest, length, 0x0,	/* dst */
			     buf, bufsz, offset,	/* src */
			     length, error))
		return FALSE;
	for (guint i = 0; i < length; i++)
		g_string_append_printf (csum, "%02x", digest[i]);
	return g_string_free (g_steal_pointer (&csum), FALSE);
}

static gboolean
fu_tpm_eventlog_device_parse_blob_v2 (FuTpmEventlogDevice *self,
				      const guint8 *buf, gsize bufsz,
				      GError **error)
{
	guint32 hdrsz = 0x0;

	/* advance over the header block */
	if (!fu_common_read_uint32_safe	(buf, bufsz,
					 FU_TPM_EVENTLOG_V1_IDX_EVENT_SIZE,
					 &hdrsz, G_LITTLE_ENDIAN, error))
		return FALSE;
	for (gsize idx = FU_TPM_EVENTLOG_V1_SIZE + hdrsz; idx < bufsz;) {
		guint32 pcr = 0;
		guint32 event_type = 0;
		guint32 digestcnt = 0;
		guint32 datasz = 0;
		g_autofree gchar *csum = NULL;

		/* read entry */
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V2_IDX_PCR,
						 &pcr, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V2_IDX_TYPE,
						 &event_type, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V2_IDX_DIGEST_COUNT,
						 &digestcnt, G_LITTLE_ENDIAN, error))
			return FALSE;

		/* read checksum block */
		idx += FU_TPM_EVENTLOG_V2_SIZE;
		for (guint i = 0; i < digestcnt; i++) {
			guint16 alg_type = 0;
			guint32 alg_size = 0;
			g_autofree gchar *csum_tmp = NULL;

			/* get checksum type */
			if (!fu_common_read_uint16_safe	(buf, bufsz, idx,
							 &alg_type, G_LITTLE_ENDIAN, error))
				return FALSE;
			alg_size = fu_tpm_eventlog_hash_get_size (alg_type);
			if (alg_size == 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "hash algorithm 0x%x size not known",
					     alg_type);
				return FALSE;
			}

			/* build checksum */
			idx += sizeof(alg_type);
			csum_tmp = fu_common_read_strhex_safe (buf, bufsz, idx, alg_size, error);
			if (csum_tmp == NULL)
				return FALSE;

			/* save this for analysis */
			if (alg_type == TPM2_ALG_SHA1)
				csum = g_steal_pointer (&csum_tmp);

			/* next block */
			idx += alg_size;
		}

		/* read data block */
		if (!fu_common_read_uint32_safe	(buf, bufsz, idx,
						 &datasz, G_LITTLE_ENDIAN, error))
			return FALSE;

		/* save blob if PCR=0 */
		idx += sizeof(datasz);
		if (pcr == ESYS_TR_PCR0) {
			g_autofree guint8 *data = NULL;
			FuTpmEventlogDeviceItem *item;

			/* build item */
			data = g_malloc0 (datasz);
			if (!fu_memcpy_safe (data, datasz, 0x0,		/* dst */
					     buf, bufsz, idx, datasz,	/* src */
					     error))
				return FALSE;

			/* not normally required */
			if (g_getenv ("FWUPD_TPM_EVENTLOG_VERBOSE") != NULL) {
				fu_common_dump_full (G_LOG_DOMAIN, "Event Data",
						     data, datasz, 20,
						     FU_DUMP_FLAGS_SHOW_ASCII);
			}
			item = g_new0 (FuTpmEventlogDeviceItem, 1);
			item->kind = event_type;
			item->checksum = g_steal_pointer (&csum);
			item->blob = g_bytes_new_take (g_steal_pointer (&data), datasz);
			g_ptr_array_add (self->items, item);
		}

		/* next entry */
		idx += datasz;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_tpm_eventlog_device_parse_blob (FuTpmEventlogDevice *self,
				   const guint8 *buf, gsize bufsz,
				   GError **error)
{
	gchar sig[] = FU_TPM_EVENTLOG_V2_HDR_SIGNATURE;

	/* look for TCG v2 signature */
	if (!fu_memcpy_safe ((guint8 *) sig, sizeof(sig), 0x0,		/* dst */
			     buf, bufsz, FU_TPM_EVENTLOG_V1_SIZE,	/* src */
			     sizeof(sig), error))
		return FALSE;
	if (g_strcmp0 (sig, FU_TPM_EVENTLOG_V2_HDR_SIGNATURE) == 0)
		return fu_tpm_eventlog_device_parse_blob_v2 (self, buf, bufsz, error);

	/* assume v1 structure */
	for (gsize idx = 0; idx < bufsz; idx += FU_TPM_EVENTLOG_V1_SIZE) {
		guint32 datasz = 0;
		guint32 pcr = 0;
		guint32 event_type = 0;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V1_IDX_PCR,
						 &pcr, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V1_IDX_TYPE,
						 &event_type, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (!fu_common_read_uint32_safe	(buf, bufsz,
						 idx + FU_TPM_EVENTLOG_V1_IDX_EVENT_SIZE,
						 &datasz, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (pcr == ESYS_TR_PCR0) {
			FuTpmEventlogDeviceItem *item;
			g_autofree gchar *csum = NULL;
			g_autofree guint8 *data = NULL;

			/* build checksum */
			csum = fu_common_read_strhex_safe (buf, bufsz,
							   idx + FU_TPM_EVENTLOG_V1_IDX_DIGEST,
							   TPM2_SHA1_DIGEST_SIZE,
							   error);
			if (csum == NULL)
				return FALSE;

			/* build item */
			data = g_malloc0 (datasz);
			if (!fu_memcpy_safe (data, datasz, 0x0,			/* dst */
					     buf, bufsz, idx + FU_TPM_EVENTLOG_V1_SIZE, datasz,	/* src */
					     error))
				return FALSE;
			item = g_new0 (FuTpmEventlogDeviceItem, 1);
			item->kind = event_type;
			item->checksum = g_steal_pointer (&csum);
			item->blob = g_bytes_new_take (g_steal_pointer (&data), datasz);
			g_ptr_array_add (self->items, item);

			/* not normally required */
			if (g_getenv ("FWUPD_TPM_EVENTLOG_VERBOSE") != NULL)
				fu_common_dump_bytes (G_LOG_DOMAIN, "Event Data", item->blob);
		}
		idx += datasz;
	}
	return TRUE;
}

FuTpmEventlogDevice *
fu_tpm_eventlog_device_new (const guint8 *buf, gsize bufsz, GError **error)
{
	g_autoptr(FuTpmEventlogDevice) self = NULL;

	g_return_val_if_fail (buf != NULL, NULL);

	/* create object */
	self = g_object_new (FU_TYPE_TPM_EVENTLOG_DEVICE, NULL);
	if (!fu_tpm_eventlog_device_parse_blob (self, buf, bufsz, error))
		return NULL;
	return FU_TPM_EVENTLOG_DEVICE (g_steal_pointer (&self));
}
