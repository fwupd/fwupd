/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

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
/**
 * FU_DFU_DEVICE_FLAG_INDEX_FORCE_DETACH:
 *
 * Requires Force Detach in wIndex to bypass status checking.
 */
#define FU_DFU_DEVICE_FLAG_INDEX_FORCE_DETACH (1ull << (8 + 19))

/* helpers */
GBytes *
fu_dfu_utils_bytes_join_array(GPtrArray *chunks);
