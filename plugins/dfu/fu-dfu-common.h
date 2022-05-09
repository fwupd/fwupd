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
	FU_DFU_REQUEST_DETACH = 0x00,
	FU_DFU_REQUEST_DNLOAD = 0x01,
	FU_DFU_REQUEST_UPLOAD = 0x02,
	FU_DFU_REQUEST_GETSTATUS = 0x03,
	FU_DFU_REQUEST_CLRSTATUS = 0x04,
	FU_DFU_REQUEST_GETSTATE = 0x05,
	FU_DFU_REQUEST_ABORT = 0x06,
	/*< private >*/
	FU_DFU_REQUEST_LAST
} FuDfuRequest;

/**
 * FuDfuStatus:
 * @FU_DFU_STATUS_OK:				No error condition is present
 * @FU_DFU_STATUS_ERR_TARGET:			File is not targeted for use by this device
 * @FU_DFU_STATUS_ERR_FILE:			File is for this device but fails a verification
 *test
 * @FU_DFU_STATUS_ERR_WRITE:			Device is unable to write memory
 * @FU_DFU_STATUS_ERR_ERASE:			Memory erase function failed
 * @FU_DFU_STATUS_ERR_CHECK_ERASED:		Memory erase check failed
 * @FU_DFU_STATUS_ERR_PROG:			Program memory function failed
 * @FU_DFU_STATUS_ERR_VERIFY:			Programmed memory failed verification
 * @FU_DFU_STATUS_ERR_ADDRESS:			Cannot program memory due to received address that
 *isout of range
 * @FU_DFU_STATUS_ERR_NOTDONE:			Received DFU_DNLOAD with wLength = 0 but data is
 *incomplete
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
	FU_DFU_STATUS_OK = 0x00,
	FU_DFU_STATUS_ERR_TARGET = 0x01,
	FU_DFU_STATUS_ERR_FILE = 0x02,
	FU_DFU_STATUS_ERR_WRITE = 0x03,
	FU_DFU_STATUS_ERR_ERASE = 0x04,
	FU_DFU_STATUS_ERR_CHECK_ERASED = 0x05,
	FU_DFU_STATUS_ERR_PROG = 0x06,
	FU_DFU_STATUS_ERR_VERIFY = 0x07,
	FU_DFU_STATUS_ERR_ADDRESS = 0x08,
	FU_DFU_STATUS_ERR_NOTDONE = 0x09,
	FU_DFU_STATUS_ERR_FIRMWARE = 0x0a,
	FU_DFU_STATUS_ERR_VENDOR = 0x0b,
	FU_DFU_STATUS_ERR_USBR = 0x0c,
	FU_DFU_STATUS_ERR_POR = 0x0d,
	FU_DFU_STATUS_ERR_UNKNOWN = 0x0e,
	FU_DFU_STATUS_ERR_STALLDPKT = 0x0f,
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
	FU_DFU_STATE_APP_IDLE = 0x00,
	FU_DFU_STATE_APP_DETACH = 0x01,
	FU_DFU_STATE_DFU_IDLE = 0x02,
	FU_DFU_STATE_DFU_DNLOAD_SYNC = 0x03,
	FU_DFU_STATE_DFU_DNBUSY = 0x04,
	FU_DFU_STATE_DFU_DNLOAD_IDLE = 0x05,
	FU_DFU_STATE_DFU_MANIFEST_SYNC = 0x06,
	FU_DFU_STATE_DFU_MANIFEST = 0x07,
	FU_DFU_STATE_DFU_MANIFEST_WAIT_RESET = 0x08,
	FU_DFU_STATE_DFU_UPLOAD_IDLE = 0x09,
	FU_DFU_STATE_DFU_ERROR = 0x0a,
	/*< private >*/
	FU_DFU_STATE_LAST
} FuDfuState;

/**
 * FU_DFU_DEVICE_FLAG_CAN_DOWNLOAD:
 *
 * Can download from host->device.
 */
#define FU_DFU_DEVICE_FLAG_CAN_DOWNLOAD (1ull << 0)
/**
 * FU_DFU_DEVICE_FLAG_CAN_UPLOAD:
 *
 * Can upload from device->host.
 */
#define FU_DFU_DEVICE_FLAG_CAN_UPLOAD (1ull << 1)
/**
 * FU_DFU_DEVICE_FLAG_MANIFEST_TOL:
 *
 * Can answer GetStatus in manifest.
 */
#define FU_DFU_DEVICE_FLAG_MANIFEST_TOL (1ull << 2)
/**
 * FU_DFU_DEVICE_FLAG_WILL_DETACH:
 *
 * Will self-detach.
 */
#define FU_DFU_DEVICE_FLAG_WILL_DETACH (1ull << 3)
/**
 * FU_DFU_DEVICE_FLAG_CAN_ACCELERATE:
 *
 * Use a larger transfer size for speed.
 */
#define FU_DFU_DEVICE_FLAG_CAN_ACCELERATE (1ull << 7)

/**
 * FU_DFU_DEVICE_FLAG_ATTACH_EXTRA_RESET:
 *
 * Device needs resetting twice for attach.
 */
#define FU_DFU_DEVICE_FLAG_ATTACH_EXTRA_RESET (1ull << (8 + 0))
/**
 * FU_DFU_DEVICE_FLAG_ATTACH_UPLOAD_DOWNLOAD:
 *
 * An upload or download is required for attach.
 */
#define FU_DFU_DEVICE_FLAG_ATTACH_UPLOAD_DOWNLOAD (1ull << (8 + 1))
/**
 * FU_DFU_DEVICE_FLAG_FORCE_DFU_MODE:
 *
 * Force DFU mode.
 */
#define FU_DFU_DEVICE_FLAG_FORCE_DFU_MODE (1ull << (8 + 2))
/**
 * FU_DFU_DEVICE_FLAG_IGNORE_POLLTIMEOUT:
 *
 * Ignore the device download timeout.
 */
#define FU_DFU_DEVICE_FLAG_IGNORE_POLLTIMEOUT (1ull << (8 + 3))
/**
 * FU_DFU_DEVICE_FLAG_IGNORE_RUNTIME:
 *
 * Device has broken DFU runtime support.
 */
#define FU_DFU_DEVICE_FLAG_IGNORE_RUNTIME (1ull << (8 + 4))
/**
 * FU_DFU_DEVICE_FLAG_IGNORE_UPLOAD:
 *
 * Uploading from the device is broken.
 */
#define FU_DFU_DEVICE_FLAG_IGNORE_UPLOAD (1ull << (8 + 5))
/**
 * FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME:
 *
 * No DFU runtime interface is provided.
 */
#define FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME (1ull << (8 + 6))
/**
 * FU_DFU_DEVICE_FLAG_NO_GET_STATUS_UPLOAD:
 *
 * Do not do GetStatus when uploading.
 */
#define FU_DFU_DEVICE_FLAG_NO_GET_STATUS_UPLOAD (1ull << (8 + 7))
/**
 * FU_DFU_DEVICE_FLAG_NO_PID_CHANGE:
 *
 * Accept the same VID:PID when changing modes.
 */
#define FU_DFU_DEVICE_FLAG_NO_PID_CHANGE (1ull << (8 + 8))
/**
 * FU_DFU_DEVICE_FLAG_USE_ANY_INTERFACE:
 *
 * Use any interface for DFU.
 */
#define FU_DFU_DEVICE_FLAG_USE_ANY_INTERFACE (1ull << (8 + 9))
/**
 * FU_DFU_DEVICE_FLAG_USE_ATMEL_AVR:
 *
 * Device uses the ATMEL bootloader.
 */
#define FU_DFU_DEVICE_FLAG_USE_ATMEL_AVR (1ull << (8 + 10))
/**
 * FU_DFU_DEVICE_FLAG_USE_PROTOCOL_ZERO:
 *
 * Fix up the protocol number.
 */
#define FU_DFU_DEVICE_FLAG_USE_PROTOCOL_ZERO (1ull << (8 + 11))
/**
 * FU_DFU_DEVICE_FLAG_LEGACY_PROTOCOL:
 *
 * Use a legacy protocol version.
 */
#define FU_DFU_DEVICE_FLAG_LEGACY_PROTOCOL (1ull << (8 + 12))
/**
 * FU_DFU_DEVICE_FLAG_DETACH_FOR_ATTACH:
 *
 * Requires a FU_DFU_REQUEST_DETACH to attach.
 */
#define FU_DFU_DEVICE_FLAG_DETACH_FOR_ATTACH (1ull << (8 + 13))
/**
 * FU_DFU_DEVICE_FLAG_ABSENT_SECTOR_SIZE:
 *
 * In absence of sector size, assume byte.
 */
#define FU_DFU_DEVICE_FLAG_ABSENT_SECTOR_SIZE (1ull << (8 + 14))
/**
 * FU_DFU_DEVICE_FLAG_MANIFEST_POLL:
 *
 * Requires polling via GetStatus in dfuManifest state.
 */
#define FU_DFU_DEVICE_FLAG_MANIFEST_POLL (1ull << (8 + 15))
/**
 * FU_DFU_DEVICE_FLAG_NO_BUS_RESET_ATTACH:
 *
 * Do not require a bus reset to attach to normal.
 */
#define FU_DFU_DEVICE_FLAG_NO_BUS_RESET_ATTACH (1ull << (8 + 16))
/**
 * FU_DFU_DEVICE_FLAG_GD32:
 *
 * Uses the slightly weird GD32 variant of DFU.
 */
#define FU_DFU_DEVICE_FLAG_GD32 (1ull << (8 + 17))
/**
 * FU_DFU_DEVICE_FLAG_ALLOW_ZERO_POLLTIMEOUT:
 *
 * Allows the zero bwPollTimeout from GetStatus in dfuDNLOAD-SYNC state.
 */
#define FU_DFU_DEVICE_FLAG_ALLOW_ZERO_POLLTIMEOUT (1ull << (8 + 18))

const gchar *
fu_dfu_state_to_string(FuDfuState state);
const gchar *
fu_dfu_status_to_string(FuDfuStatus status);

/* helpers */
GBytes *
fu_dfu_utils_bytes_join_array(GPtrArray *chunks);
