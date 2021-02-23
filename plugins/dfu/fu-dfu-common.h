/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

/**
 * FuDfuRequest:
 * @FU_DFU_REQUEST_DETACH:			Detach
 * @FU_DFU_REQUEST_DNLOAD:			Download host-to-device
 * @FU_DFU_REQUEST_UPLOAD:			Upload device-to-host
 * @FU_DFU_REQUEST_GETSTATUS:			Get the device status
 * @FU_DFU_REQUEST_CLRSTATUS:			Clear the device status
 * @FU_DFU_REQUEST_GETSTATE:			Get the last set state
 * @FU_DFU_REQUEST_ABORT:			Abort the current transfer
 *
 * The DFU request kinds.
 **/
typedef enum {
	FU_DFU_REQUEST_DETACH			= 0x00,
	FU_DFU_REQUEST_DNLOAD			= 0x01,
	FU_DFU_REQUEST_UPLOAD			= 0x02,
	FU_DFU_REQUEST_GETSTATUS		= 0x03,
	FU_DFU_REQUEST_CLRSTATUS		= 0x04,
	FU_DFU_REQUEST_GETSTATE			= 0x05,
	FU_DFU_REQUEST_ABORT			= 0x06,
	/*< private >*/
	FU_DFU_REQUEST_LAST
} FuDfuRequest;

/**
 * FuDfuStatus:
 * @FU_DFU_STATUS_OK:				No error condition is present
 * @FU_DFU_STATUS_ERR_TARGET:			File is not targeted for use by this device
 * @FU_DFU_STATUS_ERR_FILE:			File is for this device but fails a verification test
 * @FU_DFU_STATUS_ERR_WRITE:			Device is unable to write memory
 * @FU_DFU_STATUS_ERR_ERASE:			Memory erase function failed
 * @FU_DFU_STATUS_ERR_CHECK_ERASED:		Memory erase check failed
 * @FU_DFU_STATUS_ERR_PROG:			Program memory function failed
 * @FU_DFU_STATUS_ERR_VERIFY:			Programmed memory failed verification
 * @FU_DFU_STATUS_ERR_ADDRESS:			Cannot program memory due to received address that isout of range
 * @FU_DFU_STATUS_ERR_NOTDONE:			Received DFU_DNLOAD with wLength = 0 but data is incomplete
 * @FU_DFU_STATUS_ERR_FIRMWARE:			Device firmware is corrupt
 * @FU_DFU_STATUS_ERR_VENDOR:			iString indicates a vendor-specific error
 * @FU_DFU_STATUS_ERR_USBR:			Device detected unexpected USB reset signaling
 * @FU_DFU_STATUS_ERR_POR:			Device detected unexpected power on reset
 * @FU_DFU_STATUS_ERR_UNKNOWN:			Something unexpected went wrong
 * @FU_DFU_STATUS_ERR_STALLDPKT:		Device stalled an unexpected request
 *
 * The status enumerated kind.
 **/
typedef enum {
	FU_DFU_STATUS_OK			= 0x00,
	FU_DFU_STATUS_ERR_TARGET		= 0x01,
	FU_DFU_STATUS_ERR_FILE			= 0x02,
	FU_DFU_STATUS_ERR_WRITE			= 0x03,
	FU_DFU_STATUS_ERR_ERASE			= 0x04,
	FU_DFU_STATUS_ERR_CHECK_ERASED		= 0x05,
	FU_DFU_STATUS_ERR_PROG			= 0x06,
	FU_DFU_STATUS_ERR_VERIFY		= 0x07,
	FU_DFU_STATUS_ERR_ADDRESS		= 0x08,
	FU_DFU_STATUS_ERR_NOTDONE		= 0x09,
	FU_DFU_STATUS_ERR_FIRMWARE		= 0x0a,
	FU_DFU_STATUS_ERR_VENDOR		= 0x0b,
	FU_DFU_STATUS_ERR_USBR			= 0x0c,
	FU_DFU_STATUS_ERR_POR			= 0x0d,
	FU_DFU_STATUS_ERR_UNKNOWN		= 0x0e,
	FU_DFU_STATUS_ERR_STALLDPKT		= 0x0f,
	/*< private >*/
	FU_DFU_STATUS_LAST
} FuDfuStatus;

/**
 * FuDfuState:
 * @FU_DFU_STATE_APP_IDLE:			State 0
 * @FU_DFU_STATE_APP_DETACH:			State 1
 * @FU_DFU_STATE_DFU_IDLE:			State 2
 * @FU_DFU_STATE_DFU_DNLOAD_SYNC:		State 3
 * @FU_DFU_STATE_DFU_DNBUSY:			State 4
 * @FU_DFU_STATE_DFU_DNLOAD_IDLE:		State 5
 * @FU_DFU_STATE_DFU_MANIFEST_SYNC:		State 6
 * @FU_DFU_STATE_DFU_MANIFEST:			State 7
 * @FU_DFU_STATE_DFU_MANIFEST_WAIT_RESET:	State 8
 * @FU_DFU_STATE_DFU_UPLOAD_IDLE:		State 9
 * @FU_DFU_STATE_DFU_ERROR:			State 10
 *
 * The state enumerated kind.
 **/
typedef enum {
	FU_DFU_STATE_APP_IDLE			= 0x00,
	FU_DFU_STATE_APP_DETACH			= 0x01,
	FU_DFU_STATE_DFU_IDLE			= 0x02,
	FU_DFU_STATE_DFU_DNLOAD_SYNC		= 0x03,
	FU_DFU_STATE_DFU_DNBUSY			= 0x04,
	FU_DFU_STATE_DFU_DNLOAD_IDLE		= 0x05,
	FU_DFU_STATE_DFU_MANIFEST_SYNC		= 0x06,
	FU_DFU_STATE_DFU_MANIFEST		= 0x07,
	FU_DFU_STATE_DFU_MANIFEST_WAIT_RESET	= 0x08,
	FU_DFU_STATE_DFU_UPLOAD_IDLE		= 0x09,
	FU_DFU_STATE_DFU_ERROR			= 0x0a,
	/*< private >*/
	FU_DFU_STATE_LAST
} FuDfuState;

const gchar	*fu_dfu_state_to_string			(FuDfuState	 state);
const gchar	*fu_dfu_status_to_string		(FuDfuStatus	 status);

/* helpers */
GBytes		*fu_dfu_utils_bytes_join_array		(GPtrArray	*chunks);
