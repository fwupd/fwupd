/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_TPM_EVENTLOG (fu_tpm_eventlog_get_type())
G_DECLARE_DERIVABLE_TYPE(FuTpmEventlog, fu_tpm_eventlog, FU, TPM_EVENTLOG, FuFirmware)

struct _FuTpmEventlogClass {
	FuFirmwareClass parent_class;
};

GPtrArray *
fu_tpm_eventlog_calc_checksums(FuTpmEventlog *self,
			       guint8 pcr,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
