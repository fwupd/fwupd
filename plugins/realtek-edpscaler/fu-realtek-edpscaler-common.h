/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Justin Li <justin_li@realsil.com.cn>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#define REALTEK_EDPSCALER_SMBUS_BLOCK_MAXSZ 32 /* max SMBus block payload */

/* ioctl timeout (ms) */
#define REALTEK_EDPSCALER_IOCTL_TIMEOUT_MS 500

/* timings (ms). The base delay is the per-command receive delay (default
 * 10); the various waits below are expressed as multiples of it. */
#define REALTEK_EDPSCALER_RECEIVE_CMD_DELAY_BASE_MS 10 /* per-command receive delay */
#define REALTEK_EDPSCALER_GET_VERSION_WAIT_MS                                                      \
	(REALTEK_EDPSCALER_RECEIVE_CMD_DELAY_BASE_MS * 50) /* before version readback */
#define REALTEK_EDPSCALER_CHECK_PROJECT_ID_WAIT_MS                                                 \
	(REALTEK_EDPSCALER_RECEIVE_CMD_DELAY_BASE_MS * 100) /* before project-ID readback */

/* handshake sleep after toggling block mode; unrelated to the
 * receive-command delay */
#define REALTEK_EDPSCALER_SMBUS_BLOCK_WAIT_MS  200   /* block-mode command handshake */
#define REALTEK_EDPSCALER_ATTACH_RESET_WAIT_MS 15000 /* delay after reset for new FW */

/* retry counters. With the 5ms retry delay this bounds one status poll to
 * ~25s before giving up. */
#define REALTEK_EDPSCALER_RETRY_DELAY_MS	    5 /* interval between status polls */
#define REALTEK_EDPSCALER_STATUS_POLL_RETRY_COUNT   5000
#define REALTEK_EDPSCALER_BLOCK_ENABLE_RETRY_COUNT  10
#define REALTEK_EDPSCALER_FG_MODE_ENTER_RETRY_COUNT 50
