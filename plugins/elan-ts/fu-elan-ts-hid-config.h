/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
 
#pragma once

/**
 * ELAN_TS_HID_VID:
 *
 * The USB Vendor ID (0x04F3) allocated to Elan Microelectronics Corp.
 */
#define ELAN_TS_HID_VID				0x04F3

/**
 * ELAN_TS_HID_PID_BRIDGE:
 *
 * The specific Product ID (0x0007) used for Elan TS bridge hardware,
 * which requires a different HID Report ID (0x00) for I/O transactions.
 */
#define ELAN_TS_HID_PID_BRIDGE			0x0007

/**
 * ELAN_TS_HID_PID_BRIDGE_B:
 *
 * Another specific Product ID (0x000B) used for Elan TS bridge hardware,
 * which follows the same HID Report ID conventions as PID #ELAN_TS_HID_PID_BRIDGE.
 */
#define ELAN_TS_HID_PID_BRIDGE_B		0x000B

/**
 * ELAN_TS_HID_OUTPUT_REPORT_ID:
 *
 * The standard HID Report ID used for output transactions (0x03).
 */
#define ELAN_TS_HID_OUTPUT_REPORT_ID		0x03

/**
 * ELAN_TS_HID_OUTPUT_REPORT_ID_BRIDGE:
 *
 * The specific HID Report ID used for output transactions on bridge
 * hardware (e.g., PID #ELAN_TS_HID_PID_BRIDGE).
 */
#define ELAN_TS_HID_OUTPUT_REPORT_ID_BRIDGE	0x00

/**
 * ELAN_TS_HID_INPUT_REPORT_ID:
 *
 * The standard HID Report ID used for input transactions (0x02).
 */
#define ELAN_TS_HID_INPUT_REPORT_ID		0x02

/**
 * ELAN_TS_HID_INPUT_REPORT_ID_BRIDGE:
 *
 * The specific HID Report ID used for input transactions on bridge
 * hardware (e.g., PID #ELAN_TS_HID_PID_BRIDGE).
 */
#define ELAN_TS_HID_INPUT_REPORT_ID_BRIDGE	0x00

/**
 * ELAN_TS_HID_FINGER_REPORT_ID:
 *
 * The HID Report ID used for touch finger data (0x01).
 */
#define ELAN_TS_HID_FINGER_REPORT_ID		0x01

/**
 * ELAN_TS_HID_PEN_REPORT_ID:
 *
 * The HID Report ID used for pen/stylus data (0x07).
 */
#define ELAN_TS_HID_PEN_REPORT_ID		0x07

/**
 * ELAN_TS_HID_PEN_DEBUG_REPORT_ID:
 *
 * The HID Report ID used for pen/stylus diagnostic or debug data (0x17).
 */
#define ELAN_TS_HID_PEN_DEBUG_REPORT_ID		0x17

/*
 * ELAN TS HID Buffer Size for Data
 *
 * Calculated from the maximum raw report size (65 bytes) minus:
 * - 1 byte: HID Report ID
 * - 1 byte: Protocol Data Length field
 * = 63 bytes (0x3F) available for the data buffer.
 */
#define ELAN_TS_HID_DATA_BUFFER_SIZE		0x3F

/*
 * ELAN TS HID Frame Size for Page Read
 *
 * Calculated based on the Maximum HID Input Report size minus protocol overhead:
 * Max HID Report (63 bytes)
 * - 1 byte (ELAN Packet Header: 0x99)
 * - 1 byte (ELAN Packet Index)
 * - 1 byte (ELAN Data Length)
 * = 60 bytes (0x3C) of effective data payload per HID frame.
 */
#define ELAN_TS_HID_READ_PAGE_FRAME_SIZE	0x3C

/*
 * ELAN TS HID Frame Size for IAP (In-Application Programming)
 *
 * ELAN_TS_HID_PAGE_FRAME_SIZE:
 * This defines the maximum payload size (in bytes) for a single HID frame
 * during firmware data transfer.
 *
 * Calculation Breakdown:
 * Total HID Report Size (33 bytes)
 * - 1 byte  : HID Output Report ID (0x03)
 * - 1 byte  : IAP Protocol Sub-command (e.g., 0x21)
 * - 2 bytes : MemAddr (Logical page address, NOT a physical memory address)
 * - 1 byte  : Data length field
 * = 28 bytes (or 14 Words) of raw firmware data per frame.
 */
#define ELAN_TS_HID_PAGE_FRAME_SIZE		0x1C

