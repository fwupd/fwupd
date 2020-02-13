 /*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>
#include <tss2/tss2_tpm2_types.h>

#include "fu-plugin.h"

typedef enum {
	EV_PREBOOT_CERT				= 0x00000000,
	EV_POST_CODE				= 0x00000001,
	EV_NO_ACTION				= 0x00000003,
	EV_SEPARATOR				= 0x00000004,
	EV_ACTION				= 0x00000005,
	EV_EVENT_TAG				= 0x00000006,
	EV_S_CRTM_CONTENTS			= 0x00000007,
	EV_S_CRTM_VERSION			= 0x00000008,
	EV_CPU_MICROCODE			= 0x00000009,
	EV_PLATFORM_CONFIG_FLAGS		= 0x0000000a,
	EV_TABLE_OF_DEVICES			= 0x0000000b,
	EV_COMPACT_HASH				= 0x0000000c,
	EV_NONHOST_CODE				= 0x0000000f,
	EV_NONHOST_CONFIG			= 0x00000010,
	EV_NONHOST_INFO				= 0x00000011,
	EV_OMIT_BOOT_DEVICE_EVENTS		= 0x00000012,
	EV_EFI_EVENT_BASE			= 0x80000000,
	EV_EFI_VARIABLE_DRIVER_CONFIG		= 0x80000001,
	EV_EFI_VARIABLE_BOOT			= 0x80000002,
	EV_EFI_BOOT_SERVICES_APPLICATION	= 0x80000003,
	EV_EFI_BOOT_SERVICES_DRIVER		= 0x80000004,
	EV_EFI_RUNTIME_SERVICES_DRIVER		= 0x80000005,
	EV_EFI_GPT_EVENT			= 0x80000006,
	EV_EFI_ACTION				= 0x80000007,
	EV_EFI_PLATFORM_FIRMWARE_BLOB		= 0x80000008,
	EV_EFI_HANDOFF_TABLES			= 0x80000009,
	EV_EFI_HCRTM_EVENT			= 0x80000010,
	EV_EFI_VARIABLE_AUTHORITY		= 0x800000e0
} FuTpmEventlogItemKind;

typedef struct {
	guint8			 pcr;
	FuTpmEventlogItemKind	 kind;
	GBytes			*checksum_sha1;
	GBytes			*checksum_sha256;
	GBytes			*blob;
} FuTpmEventlogItem;

const gchar 	*fu_tpm_eventlog_pcr_to_string		(gint		 pcr);
const gchar	*fu_tpm_eventlog_hash_to_string		(TPM2_ALG_ID	 hash_kind);
guint32		 fu_tpm_eventlog_hash_get_size		(TPM2_ALG_ID	 hash_kind);
const gchar	*fu_tpm_eventlog_item_kind_to_string	(FuTpmEventlogItemKind	 event_type);
gchar		*fu_tpm_eventlog_strhex			(GBytes		*blob);
gchar		*fu_tpm_eventlog_blobstr		(GBytes		*blob);
GPtrArray	*fu_tpm_eventlog_calc_checksums		(GPtrArray	*items,
							 guint8		 pcr,
							 GError		**error);
