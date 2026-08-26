/*
 * Copyright 2017 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/**
 * FU_DEVICE_METADATA_TBT_IS_SAFE_MODE:
 *
 * If the Thunderbolt hardware is stuck in safe mode.
 * Consumed by the thunderbolt plugin.
 */
#define FU_DEVICE_METADATA_TBT_IS_SAFE_MODE "Thunderbolt::IsSafeMode"

/**
 * FU_DEVICE_METADATA_UEFI_DEVICE_KIND:
 *
 * The type of UEFI device, e.g. "system-firmware" or "device-firmware"
 * Consumed by the uefi plugin when other devices register fake devices that
 * need to be handled as a capsule update.
 */
#define FU_DEVICE_METADATA_UEFI_DEVICE_KIND "UefiDeviceKind"

/**
 * FU_DEVICE_METADATA_UEFI_FW_VERSION:
 *
 * The firmware version of the UEFI device specified as a 32 bit unsigned
 * integer.
 * Consumed by the uefi plugin when other devices register fake devices that
 * need to be handled as a capsule update.
 */
#define FU_DEVICE_METADATA_UEFI_FW_VERSION "UefiFwVersion"

/**
 * FU_DEVICE_METADATA_UEFI_CAPSULE_FLAGS:
 *
 * The capsule flags for the UEFI device, e.g. %EFI_CAPSULE_HEADER_FLAGS_PERSIST_ACROSS_RESET
 * Consumed by the uefi plugin when other devices register fake devices that
 * need to be handled as a capsule update.
 */
#define FU_DEVICE_METADATA_UEFI_CAPSULE_FLAGS "UefiCapsuleFlags"

/**
 * FU_DEVICE_METADATA_UEFI_CAPSULE_ON_DISK:
 *
 * If set to %TRUE, use Capsule-on-Disk for a proxy UEFI capsule device even
 * when native firmware discovery selected another update method.
 *
 * Since: 2.2.2
 **/
#define FU_DEVICE_METADATA_UEFI_CAPSULE_ON_DISK "UefiCapsuleOnDisk"

/**
 * FU_DEVICE_METADATA_UEFI_CAPSULE_NO_RT_SET_VARIABLE:
 *
 * If set to %TRUE, do not set `OsIndications` after staging a proxy capsule.
 * This is used when a bootloader consumes Capsule-on-Disk updates without EFI
 * runtime services.
 *
 * Since: 2.2.2
 **/
#define FU_DEVICE_METADATA_UEFI_CAPSULE_NO_RT_SET_VARIABLE "UefiCapsuleNoRtSetVariable"

G_END_DECLS
