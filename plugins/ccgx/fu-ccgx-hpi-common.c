/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-hpi-common.h"

const gchar *
fu_ccgx_pd_resp_to_string(CyPDResp val)
{
	if (val == CY_PD_RESP_NO_RESPONSE)
		return "resp-no-response";
	if (val == CY_PD_RESP_SUCCESS)
		return "resp-success";
	if (val == CY_PD_RESP_FLASH_DATA_AVAILABLE)
		return "resp-flash-data-available";
	if (val == CY_PD_RESP_INVALID_COMMAND)
		return "resp-invalid-command";
	if (val == CY_PD_RESP_COLLISION_DETECTED)
		return "resp-collision-detected";
	if (val == CY_PD_RESP_FLASH_UPDATE_FAILED)
		return "resp-flash-update-failed";
	if (val == CY_PD_RESP_INVALID_FW)
		return "resp-invalid-fw";
	if (val == CY_PD_RESP_INVALID_ARGUMENTS)
		return "resp-invalid-arguments";
	if (val == CY_PD_RESP_NOT_SUPPORTED)
		return "resp-not-supported";
	if (val == CY_PD_RESP_TRANSACTION_FAILED)
		return "resp-transaction-failed";
	if (val == CY_PD_RESP_PD_COMMAND_FAILED)
		return "resp-pd-command-failed";
	if (val == CY_PD_RESP_UNDEFINED)
		return "resp-undefined";
	if (val == CY_PD_RESP_RA_DETECT)
		return "resp-ra-detect";
	if (val == CY_PD_RESP_RA_REMOVED)
		return "resp-ra-removed";
	if (val == CY_PD_RESP_RESET_COMPLETE)
		return "resp-reset-complete";
	if (val == CY_PD_RESP_MESSAGE_QUEUE_OVERFLOW)
		return "resp-message-queue-overflow";
	if (val == CY_PD_RESP_OVER_CURRENT_DETECTED)
		return "resp-over-current-detected";
	if (val == CY_PD_RESP_OVER_VOLTAGE_DETECTED)
		return "resp-over-voltage-detected";
	if (val == CY_PD_RESP_TYPC_C_CONNECTED)
		return "resp-typc-c-connected";
	if (val == CY_PD_RESP_TYPE_C_DISCONNECTED)
		return "resp-type-c-disconnected";
	if (val == CY_PD_RESP_PD_CONTRACT_ESTABLISHED)
		return "resp-pd-contract-established";
	if (val == CY_PD_RESP_DR_SWAP)
		return "resp-dr-swap";
	if (val == CY_PD_RESP_PR_SWAP)
		return "resp-pr-swap";
	if (val == CY_PD_RESP_VCON_SWAP)
		return "resp-vcon-swap";
	if (val == CY_PD_RESP_PS_RDY)
		return "resp-ps-rdy";
	if (val == CY_PD_RESP_GOTOMIN)
		return "resp-gotomin";
	if (val == CY_PD_RESP_ACCEPT_MESSAGE)
		return "resp-accept-message";
	if (val == CY_PD_RESP_REJECT_MESSAGE)
		return "resp-reject-message";
	if (val == CY_PD_RESP_WAIT_MESSAGE)
		return "resp-wait-message";
	if (val == CY_PD_RESP_HARD_RESET)
		return "resp-hard-reset";
	if (val == CY_PD_RESP_VDM_RECEIVED)
		return "resp-vdm-received";
	if (val == CY_PD_RESP_SRC_CAP_RCVD)
		return "resp-src-cap-rcvd";
	if (val == CY_PD_RESP_SINK_CAP_RCVD)
		return "resp-sink-cap-rcvd";
	if (val == CY_PD_RESP_DP_ALTERNATE_MODE)
		return "resp-dp-alternate-mode";
	if (val == CY_PD_RESP_DP_DEVICE_CONNECTED)
		return "resp-dp-device-connected";
	if (val == CY_PD_RESP_DP_DEVICE_NOT_CONNECTED)
		return "resp-dp-device-not-connected";
	if (val == CY_PD_RESP_DP_SID_NOT_FOUND)
		return "resp-dp-sid-not-found";
	if (val == CY_PD_RESP_MULTIPLE_SVID_DISCOVERED)
		return "resp-multiple-svid-discovered";
	if (val == CY_PD_RESP_DP_FUNCTION_NOT_SUPPORTED)
		return "resp-dp-function-not-supported";
	if (val == CY_PD_RESP_DP_PORT_CONFIG_NOT_SUPPORTED)
		return "resp-dp-port-config-not-supported";
	if (val == CY_PD_HARD_RESET_SENT)
		return "hard-reset-sent";
	if (val == CY_PD_SOFT_RESET_SENT)
		return "soft-reset-sent";
	if (val == CY_PD_CABLE_RESET_SENT)
		return "cable-reset-sent";
	if (val == CY_PD_SOURCE_DISABLED_STATE_ENTERED)
		return "source-disabled-state-entered";
	if (val == CY_PD_SENDER_RESPONSE_TIMER_TIMEOUT)
		return "sender-response-timer-timeout";
	if (val == CY_PD_NO_VDM_RESPONSE_RECEIVED)
		return "no-vdm-response-received";
	return NULL;
}
