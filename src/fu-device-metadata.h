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
 * FU_DEVICE_METADATA_DELL_DOCK_TYPE:
 *
 * The type of dock plugged into the system
 * (if any)
 * Consumed by the synaptics plugin.
 */
#define FU_DEVICE_METADATA_DELL_DOCK_TYPE	"Dell::DockType"

#endif /* __FU_DEVICE_METADATA_H__ */
