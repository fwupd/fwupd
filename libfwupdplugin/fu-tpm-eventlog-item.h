/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"
#include "fu-tpm-struct.h"

#define FU_TYPE_TPM_EVENTLOG_ITEM (fu_tpm_eventlog_item_get_type())

G_DECLARE_FINAL_TYPE(FuTpmEventlogItem, fu_tpm_eventlog_item, FU, TPM_EVENTLOG_ITEM, FuFirmware)

FuTpmEventlogItem *
fu_tpm_eventlog_item_new(void) G_GNUC_WARN_UNUSED_RESULT;

FuTpmEventlogItemKind
fu_tpm_eventlog_item_get_kind(FuTpmEventlogItem *self) G_GNUC_NON_NULL(1);
void
fu_tpm_eventlog_item_set_kind(FuTpmEventlogItem *self, FuTpmEventlogItemKind kind)
    G_GNUC_NON_NULL(1);
guint8
fu_tpm_eventlog_item_get_pcr(FuTpmEventlogItem *self) G_GNUC_NON_NULL(1);
void
fu_tpm_eventlog_item_set_pcr(FuTpmEventlogItem *self, guint8 pcr) G_GNUC_NON_NULL(1);
void
fu_tpm_eventlog_item_add_checksum(FuTpmEventlogItem *self, FuTpmAlg alg, GBytes *checksum)
    G_GNUC_NON_NULL(1, 3);
GBytes *
fu_tpm_eventlog_item_get_checksum(FuTpmEventlogItem *self,
				  FuTpmAlg alg,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
