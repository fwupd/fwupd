/* -*- mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_DEVICE_METADATA_H__
#define __FU_DEVICE_METADATA_H__

/**
 * SECTION:fu-device-metadata
 * @short_description: a device helper object
 *
 * An object that makes it easy to close a device when an object goes out of
 * scope.
 *
 * See also: #FuDevice
 */

/**
 * FU_DEVICE_METADATA_TBT_CAN_FORCE_POWER:
 *
 * If the system can force-enable the Thunderbolt controller.
 * Consumed by the thunderbolt plugin.
 */
#define FU_DEVICE_METADATA_TBT_CAN_FORCE_POWER	"Thunderbolt::CanForcePower"

/**
 * FU_DEVICE_METADATA_TBT_IS_SAFE_MODE:
 *
 * If the Thunderbolt hardware is stuck in safe mode.
 * Consumed by the thunderbolt plugin.
 */
#define FU_DEVICE_METADATA_TBT_IS_SAFE_MODE	"Thunderbolt::IsSafeMode"

/**
 * FU_DEVICE_METADATA_UEFI_DEVICE_KIND:
 *
 * The type of UEFI device, e.g. "system-firmware" or "device-firmware"
 * Consumed by the uefi plugin when other devices register fake devices that
 * need to be handled as a capsule update.
 */
#define FU_DEVICE_METADATA_UEFI_DEVICE_KIND	"UefiDeviceKind"

#endif /* __FU_DEVICE_METADATA_H__ */
