/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>
#include <gusb.h>

/**
 * DfuRequest:
 * @DFU_REQUEST_DETACH:				Detach
 * @DFU_REQUEST_DNLOAD:				Download host-to-device
 * @DFU_REQUEST_UPLOAD:				Upload device-to-host
 * @DFU_REQUEST_GETSTATUS:			Get the device status
 * @DFU_REQUEST_CLRSTATUS:			Clear the device status
 * @DFU_REQUEST_GETSTATE:			Get the last set state
 * @DFU_REQUEST_ABORT:				Abort the current transfer
 *
 * The DFU request kinds.
 **/
typedef enum {
	DFU_REQUEST_DETACH			= 0x00,
	DFU_REQUEST_DNLOAD			= 0x01,
	DFU_REQUEST_UPLOAD			= 0x02,
	DFU_REQUEST_GETSTATUS			= 0x03,
	DFU_REQUEST_CLRSTATUS			= 0x04,
	DFU_REQUEST_GETSTATE			= 0x05,
	DFU_REQUEST_ABORT			= 0x06,
	/*< private >*/
	DFU_REQUEST_LAST
} DfuRequest;

/**
 * DfuStatus:
 * @DFU_STATUS_OK:				No error condition is present
 * @DFU_STATUS_ERR_TARGET:			File is not targeted for use by this device
 * @DFU_STATUS_ERR_FILE:			File is for this device but fails a verification test
 * @DFU_STATUS_ERR_WRITE:			Device is unable to write memory
 * @DFU_STATUS_ERR_ERASE:			Memory erase function failed
 * @DFU_STATUS_ERR_CHECK_ERASED:		Memory erase check failed
 * @DFU_STATUS_ERR_PROG:			Program memory function failed
 * @DFU_STATUS_ERR_VERIFY:			Programmed memory failed verification
 * @DFU_STATUS_ERR_ADDRESS:			Cannot program memory due to received address that isout of range
 * @DFU_STATUS_ERR_NOTDONE:			Received DFU_DNLOAD with wLength = 0 but data is incomplete
 * @DFU_STATUS_ERR_FIRMWARE:			Device firmware is corrupt
 * @DFU_STATUS_ERR_VENDOR:			iString indicates a vendor-specific error
 * @DFU_STATUS_ERR_USBR:			Device detected unexpected USB reset signaling
 * @DFU_STATUS_ERR_POR:				Device detected unexpected power on reset
 * @DFU_STATUS_ERR_UNKNOWN:			Something unexpected went wrong
 * @DFU_STATUS_ERR_STALLDPKT:			Device stalled an unexpected request
 *
 * The status enumerated kind.
 **/
typedef enum {
	DFU_STATUS_OK				= 0x00,
	DFU_STATUS_ERR_TARGET			= 0x01,
	DFU_STATUS_ERR_FILE			= 0x02,
	DFU_STATUS_ERR_WRITE			= 0x03,
	DFU_STATUS_ERR_ERASE			= 0x04,
	DFU_STATUS_ERR_CHECK_ERASED		= 0x05,
	DFU_STATUS_ERR_PROG			= 0x06,
	DFU_STATUS_ERR_VERIFY			= 0x07,
	DFU_STATUS_ERR_ADDRESS			= 0x08,
	DFU_STATUS_ERR_NOTDONE			= 0x09,
	DFU_STATUS_ERR_FIRMWARE			= 0x0a,
	DFU_STATUS_ERR_VENDOR			= 0x0b,
	DFU_STATUS_ERR_USBR			= 0x0c,
	DFU_STATUS_ERR_POR			= 0x0d,
	DFU_STATUS_ERR_UNKNOWN			= 0x0e,
	DFU_STATUS_ERR_STALLDPKT		= 0x0f,
	/*< private >*/
	DFU_STATUS_LAST
} DfuStatus;

/**
 * DfuState:
 * @DFU_STATE_APP_IDLE:				State 0
 * @DFU_STATE_APP_DETACH:			State 1
 * @DFU_STATE_DFU_IDLE:				State 2
 * @DFU_STATE_DFU_DNLOAD_SYNC:			State 3
 * @DFU_STATE_DFU_DNBUSY:			State 4
 * @DFU_STATE_DFU_DNLOAD_IDLE:			State 5
 * @DFU_STATE_DFU_MANIFEST_SYNC:		State 6
 * @DFU_STATE_DFU_MANIFEST:			State 7
 * @DFU_STATE_DFU_MANIFEST_WAIT_RESET:		State 8
 * @DFU_STATE_DFU_UPLOAD_IDLE:			State 9
 * @DFU_STATE_DFU_ERROR:			State 10
 *
 * The state enumerated kind.
 **/
typedef enum {
	DFU_STATE_APP_IDLE			= 0x00,
	DFU_STATE_APP_DETACH			= 0x01,
	DFU_STATE_DFU_IDLE			= 0x02,
	DFU_STATE_DFU_DNLOAD_SYNC		= 0x03,
	DFU_STATE_DFU_DNBUSY			= 0x04,
	DFU_STATE_DFU_DNLOAD_IDLE		= 0x05,
	DFU_STATE_DFU_MANIFEST_SYNC		= 0x06,
	DFU_STATE_DFU_MANIFEST			= 0x07,
	DFU_STATE_DFU_MANIFEST_WAIT_RESET	= 0x08,
	DFU_STATE_DFU_UPLOAD_IDLE		= 0x09,
	DFU_STATE_DFU_ERROR			= 0x0a,
	/*< private >*/
	DFU_STATE_LAST
} DfuState;

/**
 * DfuVersion:
 * @DFU_VERSION_UNKNOWN:			Format unknown
 * @DFU_VERSION_DFU_1_0:			DFU 1.0
 * @DFU_VERSION_DFU_1_1:			DFU 1.1
 * @DFU_VERSION_DFUSE:				DfuSe
 * @DFU_VERSION_ATMEL_AVR:			Atmel AVR
 *
 * The known versions of the DFU standard in BCD format.
 **/
typedef enum {
	DFU_VERSION_UNKNOWN			= 0,
	DFU_VERSION_DFU_1_0			= 0x0100,
	DFU_VERSION_DFU_1_1			= 0x0110,
	DFU_VERSION_DFUSE			= 0x011a, /* defined by ST */
	DFU_VERSION_ATMEL_AVR			= 0xff01, /* made up */
	/*< private >*/
	DFU_VERSION_LAST
} DfuVersion;

const gchar	*dfu_state_to_string			(DfuState	 state);
const gchar	*dfu_status_to_string			(DfuStatus	 status);
const gchar	*dfu_version_to_string			(DfuVersion	 version);

/* helpers */
GBytes		*dfu_utils_bytes_join_array		(GPtrArray	*chunks);
