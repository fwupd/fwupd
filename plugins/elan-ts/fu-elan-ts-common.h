/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
 
#pragma once

#include <glib.h>
#include "fu-elan-ts-struct.h"
#include "fu-elan-ts-debug.h"
#include "fu-elan-ts-hid-config.h"
#include "fu-elan-ts-hid-hw-param.h"
#include "fu-elan-ts-mem-info.h"

/* ELAN Touchscreen 8-bit I2C slave address (Default) */
#define ELAN_TS_I2C_SLAVE_ADDR        0x20

/* ELAN Touchscreen 7-bit I2C slave address (Shifted for specific drivers) */
#define ELAN_TS_I2C_SLAVE_ADDR_7BIT  (ELAN_TS_I2C_SLAVE_ADDR >> 1)

/**
 * ELAN_TS_OUTPUT_REPORT_SIZE:
 *
 * The fixed output report size (33 bytes) required by ELAN Touchscreen I/O.
 */
#define ELAN_TS_OUTPUT_REPORT_SIZE       33

/**
 * ELAN_TS_INPUT_REPORT_SIZE:
 *
 * The fixed input report size (65 bytes) required by ELAN Touchscreen I/O.
 */
#define ELAN_TS_INPUT_REPORT_SIZE        65

/**
 * ELAN_TS_DEFAULT_TRANSFER_TIMEOUT_MS:
 *
 * The default HID transfer timeout in milliseconds.
 */
#define ELAN_TS_DEFAULT_TRANSFER_TIMEOUT_MS      2000

/**
 * ELAN_TS_DEFAULT_RETRY_INTERVAL_MS:
 *
 * The default interval in milliseconds between I/O retry attempts (50ms).
 */
#define ELAN_TS_DEFAULT_RETRY_INTERVAL_MS        100

/**
 * ELAN_TS_WRITE_DATA_TIMEOUT_MS:
 *
 * The timeout in milliseconds for writing data to the ELAN touchscreen.
 */
#define ELAN_TS_WRITE_DATA_TIMEOUT_MS            1000

/**
 * ELAN_TS_READ_DATA_TIMEOUT_MS:
 *
 * The timeout in milliseconds for reading data from the ELAN touchscreen.
 */
#define ELAN_TS_READ_DATA_TIMEOUT_MS             1000

/**
 * ELAN_TS_IO_MAX_RETRIES:
 *
 * The maximum number of retry attempts for failed I/O transactions.
 */
#define ELAN_TS_IO_MAX_RETRIES               3U

/**
 * ELAN_TS_DEFAULT_ERROR_RETRY_COUNT:
 *
 * The default number of retry attempts for any type of failure, 
 * including I/O errors and invalid data patterns (3).
 */
#define ELAN_TS_DEFAULT_ERROR_RETRY_COUNT        3U

