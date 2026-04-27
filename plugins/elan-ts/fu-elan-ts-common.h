/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-elan-ts-hid-config.h"
#include "fu-elan-ts-mem-info.h"
#include "fu-elan-ts-struct.h"

/**
 * ELAN_TS_OUTPUT_REPORT_SIZE:
 *
 * The fixed output report size (33 bytes) required by ELAN Touchscreen I/O.
 */
#define ELAN_TS_OUTPUT_REPORT_SIZE 33

/**
 * ELAN_TS_INPUT_REPORT_SIZE:
 *
 * The fixed input report size (65 bytes) required by ELAN Touchscreen I/O.
 */
#define ELAN_TS_INPUT_REPORT_SIZE 65

/**
 * ELAN_TS_SELF_RESTART_DELAY_MS:
 *
 * The post-write hardware cooldown delay in milliseconds after self-restart (1000ms).
 */
#define ELAN_TS_SELF_RESTART_DELAY_MS 1000

/**
 * ELAN_TS_IO_MAX_RETRIES:
 *
 * The maximum number of retry attempts for failed I/O transactions.
 */
#define ELAN_TS_IO_MAX_RETRIES 3U

/**
 * ELAN_TS_DEFAULT_ERROR_RETRY_COUNT:
 *
 * The default number of retry attempts for any type of failure,
 * including I/O errors and invalid data patterns (3).
 */
#define ELAN_TS_DEFAULT_ERROR_RETRY_COUNT 3U
