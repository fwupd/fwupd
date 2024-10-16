/**
 * MNT Research Reset Interface public API.
 * Can be used by firmware but also by host-side tools (f.e. fwupd).
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 * Copyright (c) 2024 Chris Hofstaedtler
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <stdint.h>

// VENDOR sub-class for the reset interface
#define MNTRE_RESET_INTERFACE_SUBCLASS 0x00
// VENDOR protocol for the reset interface
#define MNTRE_RESET_INTERFACE_PROTOCOL 0x01

// CONTROL requests:
// reset to BOOTSEL
#define MNTRE_RESET_REQUEST_BOOTSEL 0x01
// reset into application
#define MNTRE_RESET_REQUEST_RESET 0x02
// read current firmware version. Returns mntre_reset_firmware_version.
#define MNTRE_RESET_GET_FIRMWARE_VERSION 0x03

// String name of the interface
#define MNTRE_RESET_INTERFACE_NAME_STR "Reset"

struct __attribute__((packed)) mntre_reset_firmware_version {
	uint32_t major;
	uint32_t minor;
	uint32_t patch;
};
