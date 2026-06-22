/*
 * Copyright 2026 Lukas Voegl <lvoegl@tdt.de>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QC_FIREHOSE_FIRMWARE (fu_qc_firehose_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuQcFirehoseFirmware,
		     fu_qc_firehose_firmware,
		     FU,
		     QC_FIREHOSE_FIRMWARE,
		     FuZipFirmware)

FuQcFirehoseFirmware *
fu_qc_firehose_firmware_new(void);
void
fu_qc_firehose_firmware_set_image_id(FuQcFirehoseFirmware *self, guint32 image_id);
void
fu_qc_firehose_firmware_add_allowed_pending_image_id(FuQcFirehoseFirmware *self, guint32 image_id);
gboolean
fu_qc_firehose_firmware_allows_pending_image(FuQcFirehoseFirmware *self);
