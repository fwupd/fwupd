/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
 
#pragma once

/**
 * ELAN_TS_HID_NORMAL_MODE_HELLO_PACKET:
 *
 * The hello packet value (0x20) returned by the device when it is 
 * running in normal mode.
 */
#define ELAN_TS_HID_NORMAL_MODE_HELLO_PACKET     0x20

/**
 * ELAN_TS_HID_RECOVERY_MODE_HELLO_PACKET:
 *
 * The hello packet value (0x56) returned by the device when it is 
 * in recovery mode, typically waiting for a firmware update.
 */
#define ELAN_TS_HID_RECOVERY_MODE_HELLO_PACKET   0x56

/*
 * Solution ID (High Byte of FW Version)
 */

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3300x1:
 *
 * Solution ID for EKTH3300x1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3300x1       0x90

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3300x2:
 *
 * Solution ID for EKTH3300x2.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3300x2       0x00

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3300x3:
 *
 * Solution ID for EKTH3300x3.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3300x3       0x01

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3300x3HV:
 *
 * Solution ID for EKTH3300x3 (High Voltage).
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3300x3HV     0x02

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3900x1:
 *
 * Solution ID for EKTH3900x1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3900x1       0x10

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3900x2:
 *
 * Solution ID for EKTH3900x2.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3900x2       0x11

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3900x3:
 *
 * Solution ID for EKTH3900x3.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3900x3       0x12

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3900x3HV:
 *
 * Solution ID for EKTH3900x3 (High Voltage).
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3900x3HV     0x13

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3915M:
 *
 * Solution ID for EKTH3915M.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3915M        0x14

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3920:
 *
 * Solution ID for EKTH3920.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3920         0x20

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH3260x1:
 *
 * Solution ID for EKTH3260x1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH3260x1       0x30

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTA5200x1:
 *
 * Solution ID for EKTA5200x1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTA5200x1       0x50

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTA5200x2:
 *
 * Solution ID for EKTA5200x2.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTA5200x2       0x51

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTA5200x3:
 *
 * Solution ID for EKTA5200x3.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTA5200x3       0x52

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTA53XXx1:
 *
 * Solution ID for EKTA53XXx1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTA53XXx1       0x55

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTA5312x1:
 *
 * Solution ID for EKTA5312x1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTA5312x1       0x56

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTA5312x2:
 *
 * Solution ID for EKTA5312x2.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTA5312x2       0x57

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTA5312x3:
 *
 * Solution ID for EKTA5312x3.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTA5312x3       0x58

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH6315x1:
 *
 * Solution ID for EKTH6315x1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH6315x1       0x61

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH6315x2:
 *
 * Solution ID for EKTH6315x2.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH6315x2       0x62

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH6315_TO_5015M:
 *
 * Solution ID for EKTH6315 remarked to 5015M.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH6315_TO_5015M 0x59

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH6315_TO_3915P:
 *
 * Solution ID for EKTH6315 remarked to 3915P.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH6315_TO_3915P 0x15

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH6308x1:
 *
 * Solution ID for EKTH6308x1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH6308x1       0x63

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH7315x1:
 *
 * Solution ID for EKTH7315x1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH7315x1       0x64

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH7315x2:
 *
 * Solution ID for EKTH7315x2.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH7315x2       0x65

/**
 * ELAN_TS_HID_SOLUTION_ID_EKTH7318x1:
 *
 * Solution ID for EKTH7318x1.
 */
#define ELAN_TS_HID_SOLUTION_ID_EKTH7318x1       0x67

/*
 * High Byte of Boot Code Version (Recovery Mode)
 */

/**
 * ELAN_TS_HID_BC_VER_H_NIBBLE_EKTA5312B_I2C:
 *
 * High nibble of BC version for EKTA5312b (I2C/USB).
 */
#define ELAN_TS_HID_BC_VER_H_NIBBLE_EKTA5312B_I2C 0x6

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312bx1_HID:
 *
 * High byte of BC version for EKTA5312bx1 (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312bx1_HID 0xA5

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312bx2_HID:
 *
 * High byte of BC version for EKTA5312bx2 (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312bx2_HID 0xB5

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312bx3_HID:
 *
 * High byte of BC version for EKTA5312bx3 (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312bx3_HID 0xC5

/**
 * ELAN_TS_HID_BC_VER_H_NIBBLE_EKTA5312C_I2C:
 *
 * High nibble of BC version for EKTA5312c (I2C/USB).
 */
#define ELAN_TS_HID_BC_VER_H_NIBBLE_EKTA5312C_I2C 0x7

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312cx1_HID:
 *
 * High byte of BC version for EKTA5312cx1 (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312cx1_HID 0xA6

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312cx2_HID:
 *
 * High byte of BC version for EKTA5312cx2 (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312cx2_HID 0xB6

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312cx3_HID:
 *
 * High byte of BC version for EKTA5312cx3 (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTA5312cx3_HID 0xC6

/**
 * ELAN_TS_HID_BC_VER_H_NIBBLE_EKTA6315_I2C:
 *
 * High nibble of BC version for EKTA6315 (I2C/USB).
 */
#define ELAN_TS_HID_BC_VER_H_NIBBLE_EKTA6315_I2C  0x8

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTA6315_HID:
 *
 * High byte of BC version for EKTA6315 (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTA6315_HID    0xA7

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTH6315_TO_5015M_HID:
 *
 * High byte of BC version for EKTH6315 remarked to 5015M (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTH6315_TO_5015M_HID 0xE6

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTH6315_TO_3915P_HID:
 *
 * High byte of BC version for EKTH6315 remarked to 3915P (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTH6315_TO_3915P_HID 0xF6

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTA6308_HID:
 *
 * High byte of BC version for EKTA6308 (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTA6308_HID    0xA8

/**
 * ELAN_TS_HID_BC_VER_H_BYTE_EKTA7315_HID:
 *
 * High byte of BC version for EKTA7315 (I2C-HID).
 */
#define ELAN_TS_HID_BC_VER_H_BYTE_EKTA7315_HID    0xA9

