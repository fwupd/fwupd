/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define FU_DFU_DEVICE_FLAG_CAN_DOWNLOAD		  "can-download"
#define FU_DFU_DEVICE_FLAG_CAN_UPLOAD		  "can-upload"
#define FU_DFU_DEVICE_FLAG_MANIFEST_TOL		  "manifest-tol"
#define FU_DFU_DEVICE_FLAG_WILL_DETACH		  "will-detach"
#define FU_DFU_DEVICE_FLAG_CAN_ACCELERATE	  "can-accelerate"
#define FU_DFU_DEVICE_FLAG_ATTACH_UPLOAD_DOWNLOAD "attach-upload-download"
#define FU_DFU_DEVICE_FLAG_FORCE_DFU_MODE	  "force-dfu-mode"
#define FU_DFU_DEVICE_FLAG_IGNORE_POLLTIMEOUT	  "ignore-polltimeout"
#define FU_DFU_DEVICE_FLAG_IGNORE_RUNTIME	  "ignore-runtime"
#define FU_DFU_DEVICE_FLAG_IGNORE_UPLOAD	  "ignore-upload"
#define FU_DFU_DEVICE_FLAG_NO_DFU_RUNTIME	  "no-dfu-runtime"
#define FU_DFU_DEVICE_FLAG_NO_GET_STATUS_UPLOAD	  "no-get-status-upload"
#define FU_DFU_DEVICE_FLAG_NO_PID_CHANGE	  "no-pid-change"
#define FU_DFU_DEVICE_FLAG_USE_ANY_INTERFACE	  "use-any-interface"
#define FU_DFU_DEVICE_FLAG_USE_ATMEL_AVR	  "use-atmel-avr"
#define FU_DFU_DEVICE_FLAG_USE_PROTOCOL_ZERO	  "use-protocol-zero"
#define FU_DFU_DEVICE_FLAG_LEGACY_PROTOCOL	  "legacy-protocol"
#define FU_DFU_DEVICE_FLAG_DETACH_FOR_ATTACH	  "detach-for-attach"
#define FU_DFU_DEVICE_FLAG_ABSENT_SECTOR_SIZE	  "absent-sector-size"
#define FU_DFU_DEVICE_FLAG_MANIFEST_POLL	  "manifest-poll"
#define FU_DFU_DEVICE_FLAG_NO_BUS_RESET_ATTACH	  "no-bus-reset-attach"
#define FU_DFU_DEVICE_FLAG_GD32			  "gd32"
#define FU_DFU_DEVICE_FLAG_ALLOW_ZERO_POLLTIMEOUT "allow-zero-polltimeout"
#define FU_DFU_DEVICE_FLAG_INDEX_FORCE_DETACH	  "index-force-detach"

GBytes *
fu_dfu_utils_bytes_join_array(GPtrArray *chunks);
