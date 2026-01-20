/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-tpm-eventlog.h"

#define FU_TYPE_TPM_EVENTLOG_V2 (fu_tpm_eventlog_v2_get_type())

G_DECLARE_FINAL_TYPE(FuTpmEventlogV2, fu_tpm_eventlog_v2, FU, TPM_EVENTLOG_V2, FuTpmEventlog)

FuTpmEventlog *
fu_tpm_eventlog_v2_new(void) G_GNUC_WARN_UNUSED_RESULT;
