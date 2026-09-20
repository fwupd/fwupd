/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Justin Li <justin_li@realsil.com.cn>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#define REALTEK_SCALER_BLOCK_MAXSZ 32 /* max payload per i2c transfer */

/* timings (ms). The base delay is the per-command receive delay (default
 * 10); the various waits below are expressed as multiples of it. */
#define REALTEK_SCALER_RECEIVE_CMD_DELAY_BASE_MS 10 /* per-command receive delay */
#define REALTEK_SCALER_GET_VERSION_WAIT_MS                                                         \
	(REALTEK_SCALER_RECEIVE_CMD_DELAY_BASE_MS * 50) /* before version readback */
#define REALTEK_SCALER_CHECK_PROJECT_ID_WAIT_MS                                                    \
	(REALTEK_SCALER_RECEIVE_CMD_DELAY_BASE_MS * 100) /* before project-ID readback */

#define REALTEK_SCALER_ATTACH_RESET_WAIT_MS 15000 /* delay after reset for new FW */

/* retry counters. With the 5ms retry delay this bounds one status poll to
 * ~25s before giving up. */
#define REALTEK_SCALER_RETRY_DELAY_MS		 5 /* interval between status polls */
#define REALTEK_SCALER_STATUS_POLL_RETRY_COUNT	 5000
#define REALTEK_SCALER_FG_MODE_ENTER_RETRY_COUNT 50
