/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-tpm-eventlog.h"

#define FU_TYPE_TPM_EVENTLOG_V1 (fu_tpm_eventlog_v1_get_type())

G_DECLARE_FINAL_TYPE(FuTpmEventlogV1, fu_tpm_eventlog_v1, FU, TPM_EVENTLOG_V1, FuTpmEventlog)

FuTpmEventlog *
fu_tpm_eventlog_v1_new(void) G_GNUC_WARN_UNUSED_RESULT;
