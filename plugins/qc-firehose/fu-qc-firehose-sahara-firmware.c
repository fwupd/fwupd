/*
 * Copyright 2026 Lukas Voegl <lvoegl@tdt.de>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-firehose-sahara-firmware.h"

struct _FuQcFirehoseSaharaFirmware {
	FuFirmware parent_instance;
	guint32 image_id;
	GArray *allowed_pending_image_ids;
};

G_DEFINE_TYPE(FuQcFirehoseSaharaFirmware, fu_qc_firehose_sahara_firmware, FU_TYPE_ZIP_FIRMWARE)

void
fu_qc_firehose_sahara_firmware_set_image_id(FuQcFirehoseSaharaFirmware *self, guint32 image_id)
{
	g_return_if_fail(FU_IS_QC_FIREHOSE_SAHARA_FIRMWARE(self));
	self->image_id = image_id;
}

void
fu_qc_firehose_sahara_firmware_add_allowed_pending_image_id(FuQcFirehoseSaharaFirmware *self,
							    guint32 image_id)
{
	g_return_if_fail(FU_IS_QC_FIREHOSE_SAHARA_FIRMWARE(self));
	g_array_append_val(self->allowed_pending_image_ids, image_id);
}

gboolean
fu_qc_firehose_sahara_firmware_allows_pending_image(FuQcFirehoseSaharaFirmware *self)
{
	g_return_val_if_fail(FU_IS_QC_FIREHOSE_SAHARA_FIRMWARE(self), FALSE);
	for (guint i = 0; i < self->allowed_pending_image_ids->len; i++) {
		guint32 allowed_image_id =
		    g_array_index(self->allowed_pending_image_ids, guint32, i);
		if (allowed_image_id == self->image_id)
			return TRUE;
	}

	return FALSE;
}

static void
fu_qc_firehose_sahara_firmware_export(FuFirmware *firmware,
				      FuFirmwareExportFlags flags,
				      XbBuilderNode *bn)
{
	FuQcFirehoseSaharaFirmware *self = FU_QC_FIREHOSE_SAHARA_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "image_id", self->image_id);
	if (self->allowed_pending_image_ids->len > 0) {
		for (guint i = 0; i < self->allowed_pending_image_ids->len; i++) {
			guint32 allowed_image_id =
			    g_array_index(self->allowed_pending_image_ids, guint32, i);
			fu_xmlb_builder_insert_kx(bn, "allowed_pending_image_id", allowed_image_id);
		}
	}
}

static void
fu_qc_firehose_sahara_firmware_init(FuQcFirehoseSaharaFirmware *self)
{
	self->allowed_pending_image_ids = g_array_new(FALSE, FALSE, sizeof(guint32));
}

static void
fu_qc_firehose_sahara_firmware_finalize(GObject *object)
{
	FuQcFirehoseSaharaFirmware *self = FU_QC_FIREHOSE_SAHARA_FIRMWARE(object);
	g_array_unref(self->allowed_pending_image_ids);
	G_OBJECT_CLASS(fu_qc_firehose_sahara_firmware_parent_class)->finalize(object);
}

static void
fu_qc_firehose_sahara_firmware_class_init(FuQcFirehoseSaharaFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_qc_firehose_sahara_firmware_finalize;
	firmware_class->export = fu_qc_firehose_sahara_firmware_export;
}

FuQcFirehoseSaharaFirmware *
fu_qc_firehose_sahara_firmware_new(void)
{
	return g_object_new(FU_TYPE_QC_FIREHOSE_SAHARA_FIRMWARE, NULL);
}
