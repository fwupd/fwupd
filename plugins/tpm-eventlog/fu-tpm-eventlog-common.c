/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-tpm-eventlog-common.h"

const gchar *
fu_tpm_eventlog_hash_to_string (TPM2_ALG_ID hash_kind)
{
	if (hash_kind == TPM2_ALG_SHA1)
		return "SHA1";
	if (hash_kind == TPM2_ALG_SHA256)
		return "SHA256";
	if (hash_kind == TPM2_ALG_SHA384)
		return "SHA384";
	if (hash_kind == TPM2_ALG_SHA512)
		return "SHA512";
	return NULL;
}

guint32
fu_tpm_eventlog_hash_get_size (TPM2_ALG_ID hash_kind)
{
	if (hash_kind == TPM2_ALG_SHA1)
		return TPM2_SHA1_DIGEST_SIZE;
	if (hash_kind == TPM2_ALG_SHA256)
		return TPM2_SHA256_DIGEST_SIZE;
	if (hash_kind == TPM2_ALG_SHA384)
		return TPM2_SHA384_DIGEST_SIZE;
	if (hash_kind == TPM2_ALG_SHA512)
		return TPM2_SHA512_DIGEST_SIZE;
	return 0;
}

const gchar *
fu_tpm_eventlog_item_kind_to_string (FuTpmEventlogItemKind event_type)
{
	if (event_type == EV_PREBOOT_CERT)
		return "EV_PREBOOT_CERT";
	if (event_type == EV_POST_CODE)
		return "EV_POST_CODE";
	if (event_type == EV_NO_ACTION)
		return "EV_NO_ACTION";
	if (event_type == EV_SEPARATOR)
		return "EV_SEPARATOR";
	if (event_type == EV_ACTION)
		return "EV_ACTION";
	if (event_type == EV_EVENT_TAG)
		return "EV_EVENT_TAG";
	if (event_type == EV_S_CRTM_CONTENTS)
		return "EV_S_CRTM_CONTENTS";
	if (event_type == EV_S_CRTM_VERSION)
		return "EV_S_CRTM_VERSION";
	if (event_type == EV_CPU_MICROCODE)
		return "EV_CPU_MICROCODE";
	if (event_type == EV_PLATFORM_CONFIG_FLAGS)
		return "EV_PLATFORM_CONFIG_FLAGS";
	if (event_type == EV_TABLE_OF_DEVICES)
		return "EV_TABLE_OF_DEVICES";
	if (event_type == EV_COMPACT_HASH)
		return "EV_COMPACT_HASH";
	if (event_type == EV_NONHOST_CODE)
		return "EV_NONHOST_CODE";
	if (event_type == EV_NONHOST_CONFIG)
		return "EV_NONHOST_CONFIG";
	if (event_type == EV_NONHOST_INFO)
		return "EV_NONHOST_INFO";
	if (event_type == EV_OMIT_BOOT_DEVICE_EVENTS)
		return "EV_OMIT_BOOT_DEVICE_EVENTS";
	if (event_type == EV_EFI_EVENT_BASE)
		return "EV_EFI_EVENT_BASE";
	if (event_type == EV_EFI_VARIABLE_DRIVER_CONFIG)
		return "EV_EFI_VARIABLE_DRIVER_CONFIG";
	if (event_type == EV_EFI_VARIABLE_BOOT)
		return "EV_EFI_VARIABLE_BOOT";
	if (event_type == EV_EFI_BOOT_SERVICES_APPLICATION)
		return "EV_BOOT_SERVICES_APPLICATION";
	if (event_type == EV_EFI_BOOT_SERVICES_DRIVER)
		return "EV_EFI_BOOT_SERVICES_DRIVER";
	if (event_type == EV_EFI_RUNTIME_SERVICES_DRIVER)
		return "EV_EFI_RUNTIME_SERVICES_DRIVER";
	if (event_type == EV_EFI_GPT_EVENT)
		return "EV_EFI_GPT_EVENT";
	if (event_type == EV_EFI_ACTION)
		return "EV_EFI_ACTION";
	if (event_type == EV_EFI_PLATFORM_FIRMWARE_BLOB)
		return "EV_EFI_PLATFORM_FIRMWARE_BLOB";
	if (event_type == EV_EFI_HANDOFF_TABLES)
		return "EV_EFI_HANDOFF_TABLES";
	if (event_type == EV_EFI_HCRTM_EVENT)
		return "EV_EFI_HCRTM_EVENT";
	if (event_type == EV_EFI_VARIABLE_AUTHORITY)
		return "EV_EFI_EFI_VARIABLE_AUTHORITY";
	return NULL;
}
