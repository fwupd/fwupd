/*
 * Copyright 2026 Lukas Voegl <lvoegl@tdt.de>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QC_FIREHOSE_SAHARA_FIRMWARE (fu_qc_firehose_sahara_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuQcFirehoseSaharaFirmware,
		     fu_qc_firehose_sahara_firmware,
		     FU,
		     QC_FIREHOSE_SAHARA_FIRMWARE,
		     FuZipFirmware)

FuQcFirehoseSaharaFirmware *
fu_qc_firehose_sahara_firmware_new(void);
void
fu_qc_firehose_sahara_firmware_set_image_id(FuQcFirehoseSaharaFirmware *self, guint32 image_id);
void
fu_qc_firehose_sahara_firmware_add_allowed_pending_image_id(FuQcFirehoseSaharaFirmware *self,
							    guint32 image_id);
gboolean
fu_qc_firehose_sahara_firmware_allows_pending_image(FuQcFirehoseSaharaFirmware *self);
