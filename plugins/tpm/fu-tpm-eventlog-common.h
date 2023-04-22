/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <tss2/tss2_tpm2_types.h>

#include "fu-tpm-struct.h"

typedef struct {
	guint8 pcr;
	FuTpmEventlogItemKind kind;
	GBytes *checksum_sha1;
	GBytes *checksum_sha256;
	GBytes *checksum_sha384;
	GBytes *blob;
} FuTpmEventlogItem;

const gchar *
fu_tpm_eventlog_pcr_to_string(gint pcr);
guint32
fu_tpm_eventlog_hash_get_size(TPM2_ALG_ID hash_kind);
gchar *
fu_tpm_eventlog_strhex(GBytes *blob);
gchar *
fu_tpm_eventlog_blobstr(GBytes *blob);
GPtrArray *
fu_tpm_eventlog_calc_checksums(GPtrArray *items, guint8 pcr, GError **error);
