/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <tss2/tss2_esys.h>

#include "fu-common.h"

#include "fu-tpm-eventlog-device.h"
#include "fu-tpm-eventlog-parser.h"

struct _FuTpmEventlogDevice {
	FuDevice		 parent_instance;
	GPtrArray		*items;
};

G_DEFINE_TYPE (FuTpmEventlogDevice, fu_tpm_eventlog_device, FU_TYPE_DEVICE)

GPtrArray *
fu_tpm_eventlog_device_get_checksums (FuTpmEventlogDevice *self, guint8 pcr, GError **error)
{
	return fu_tpm_eventlog_calc_checksums (self->items, pcr, error);
}

static void
fu_tpm_eventlog_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuTpmEventlogDevice *self = FU_TPM_EVENTLOG_DEVICE (device);
	if (self->items->len > 0) {
		fu_common_string_append_kv (str, idt, "Items", NULL);
		for (guint i = 0; i < self->items->len; i++) {
			FuTpmEventlogItem *item = g_ptr_array_index (self->items, i);
			fu_tpm_eventlog_item_to_string (item, idt + 1, str);
		}
	}
}

gchar *
fu_tpm_eventlog_device_report_metadata (FuTpmEventlogDevice *self)
{
	GString *str = g_string_new ("");
	g_autoptr(GPtrArray) pcrs = NULL;

	for (guint i = 0; i < self->items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index (self->items, i);
		g_autofree gchar *blobstr = fu_tpm_eventlog_blobstr (item->blob);
		g_autofree gchar *checksum = fu_tpm_eventlog_strhex (item->checksum_sha1);
		g_string_append_printf (str, "0x%08x %s", item->kind, checksum);
		if (blobstr != NULL)
			g_string_append_printf (str, " [%s]", blobstr);
		g_string_append (str, "\n");
	}
	pcrs = fu_tpm_eventlog_calc_checksums (self->items, 0, NULL);
	if (pcrs != NULL) {
		for (guint j = 0; j < pcrs->len; j++) {
			const gchar *csum = g_ptr_array_index (pcrs, j);
			g_string_append_printf (str, "PCR0: %s\n", csum);
		}
	}
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

static void
fu_tpm_eventlog_device_init (FuTpmEventlogDevice *self)
{
	fu_device_set_name (FU_DEVICE (self), "Event Log");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_physical_id (FU_DEVICE (self), "DEVNAME=/dev/tpm0");
	fu_device_set_logical_id (FU_DEVICE (self), "eventlog");
	fu_device_add_parent_guid (FU_DEVICE (self), "system-tpm");
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

FuTpmEventlogDevice *
fu_tpm_eventlog_device_new (const guint8 *buf, gsize bufsz, GError **error)
{
	g_autoptr(FuTpmEventlogDevice) self = NULL;

	g_return_val_if_fail (buf != NULL, NULL);

	/* create object */
	self = g_object_new (FU_TYPE_TPM_EVENTLOG_DEVICE, NULL);
	self->items = fu_tpm_eventlog_parser_new (buf, bufsz,
						  FU_TPM_EVENTLOG_PARSER_FLAG_NONE,
						  error);
	if (self->items == NULL)
		return NULL;
	return FU_TPM_EVENTLOG_DEVICE (g_steal_pointer (&self));
}
