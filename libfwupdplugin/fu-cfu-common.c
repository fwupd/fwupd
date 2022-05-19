/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-cfu-common.h"

/**
 * fu_cfu_device_reject_to_string:
 * @val: an enumerated value, e.g. %FU_CFU_DEVICE_REJECT_OLD_FIRMWARE
 *
 * Converts an enumerated reject type to a string.
 *
 * Returns: a string, or `unknown` for invalid
 *
 * Since: 1.7.0
 **/
const gchar *
fu_cfu_device_reject_to_string(guint8 val)
{
	if (val == FU_CFU_DEVICE_REJECT_OLD_FIRMWARE)
		return "old-firmware";
	if (val == FU_CFU_DEVICE_REJECT_INV_COMPONENT)
		return "inv-component";
	if (val == FU_CFU_DEVICE_REJECT_SWAP_PENDING)
		return "swap-pending";
	if (val == FU_CFU_DEVICE_REJECT_WRONG_BANK)
		return "wrong-bank";
	if (val == FU_CFU_DEVICE_REJECT_SIGN_RULE)
		return "sign-rule";
	if (val == FU_CFU_DEVICE_REJECT_VER_RELEASE_DEBUG)
		return "ver-release-debug";
	if (val == FU_CFU_DEVICE_REJECT_DEBUG_SAME_VERSION)
		return "debug-same-version";
	return "unknown";
}

/**
 * fu_cfu_device_offer_to_string:
 * @val: an enumerated value, e.g. %FU_CFU_DEVICE_OFFER_ACCEPT
 *
 * Converts an enumerated offer type to a string.
 *
 * Returns: a string, or `unknown` for invalid
 *
 * Since: 1.7.0
 **/
const gchar *
fu_cfu_device_offer_to_string(guint8 val)
{
	if (val == FU_CFU_DEVICE_OFFER_SKIP)
		return "skip";
	if (val == FU_CFU_DEVICE_OFFER_ACCEPT)
		return "accept";
	if (val == FU_CFU_DEVICE_OFFER_REJECT)
		return "reject";
	if (val == FU_CFU_DEVICE_OFFER_BUSY)
		return "busy";
	if (val == FU_CFU_DEVICE_OFFER_COMMAND)
		return "command";
	if (val == FU_CFU_DEVICE_OFFER_NOT_SUPPORTED)
		return "not-supported";
	return "unknown";
}

/**
 * fu_cfu_device_status_to_string:
 * @val: an enumerated value, e.g. %FU_CFU_DEVICE_OFFER_ACCEPT
 *
 * Converts an enumerated status type to a string.
 *
 * Returns: a string, or `unknown` for invalid
 *
 * Since: 1.7.0
 **/
const gchar *
fu_cfu_device_status_to_string(guint8 val)
{
	if (val == FU_CFU_DEVICE_STATUS_SUCCESS)
		return "success";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_PREPARE)
		return "error-prepare";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_WRITE)
		return "error-write";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_COMPLETE)
		return "error-complete";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_VERIFY)
		return "error-verify";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_CRC)
		return "error-crc";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_SIGNATURE)
		return "error-signature";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_VERSION)
		return "error-version";
	if (val == FU_CFU_DEVICE_STATUS_SWAP_PENDING)
		return "swap-pending";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_INVALID_ADDR)
		return "error-invalid-address";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_NO_OFFER)
		return "error-no-offer";
	if (val == FU_CFU_DEVICE_STATUS_ERROR_INVALID)
		return "error-invalid";
	return "unknown";
}
