/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#define SIZE_1KB   (1 * 1024)
#define SIZE_4KB   (4 * 1024)
#define SIZE_8KB   (8 * 1024)
#define SIZE_16KB  (16 * 1024)
#define SIZE_24KB  (24 * 1024)
#define SIZE_32KB  (32 * 1024)
#define SIZE_248KB (248 * 1024)
#define SIZE_256KB (256 * 1024)
#define SIZE_128KB (128 * 1024)
#define SIZE_144KB (144 * 1024)
#define SIZE_240KB (240 * 1024)
#define SIZE_360KB (360 * 1024)
#define SIZE_384KB (384 * 1024)
#define SIZE_512KB (512 * 1024)
#define SIZE_640KB (640 * 1024)
#define SIZE_1MB   (1024 * 1024)

#define KINETIC_FLASH_MODE_DELAY 3 /* seconds */

typedef enum {
	KT_CHIP_NONE = 0,
	KT_CHIP_BOBCAT_2800 = 1,
	KT_CHIP_BOBCAT_2850 = 2,
	KT_CHIP_PEGASUS = 3,
	KT_CHIP_MYSTIQUE = 4,
	KT_CHIP_DP2VGA = 5,
	KT_CHIP_PUMA_2900 = 6,
	KT_CHIP_PUMA_2920 = 7,
	KT_CHIP_JAGUAR_5000 = 8,
	KT_CHIP_MUSTANG_5200 = 9,
} KtChipId;

/**
 * FuKineticDpMode:
 * @FU_KINETIC_DP_MODE_UNKNOWN:    Type invalid or not known
 * @FU_KINETIC_DP_MODE_DIRECT:     Directly addressable
 * @FU_KINETIC_DP_MODE_REMOTE:     Requires remote register work
 *
 * The device type.
 **/
typedef enum {
	FU_KINETIC_DP_MODE_UNKNOWN,
	FU_KINETIC_DP_MODE_DIRECT,
	FU_KINETIC_DP_MODE_REMOTE,
	/*< private >*/
	FU_KINETIC_DP_MODE_LAST
} FuKineticDpMode;

/**
 * FuKineticDpFamily:
 * @FU_KINETIC_DP_FAMILY_UNKNOWN:  Family invalid or not known
 * @FU_KINETIC_DP_FAMILY_MUSTANG:  Mustang
 * @FU_KINETIC_DP_FAMILY_JAGUAR:   Jaguar
 *
 * The chip family.
 **/
typedef enum {
	FU_KINETIC_DP_FAMILY_UNKNOWN,

	FU_KINETIC_DP_FAMILY_MUSTANG,
	FU_KINETIC_DP_FAMILY_JAGUAR,
	/*<private >*/
	FU_KINETIC_DP_FAMILY_LAST
} FuKineticDpFamily;

const gchar *
fu_kinetic_dp_mode_to_string(FuKineticDpMode mode);
const gchar *
fu_kinetic_dp_family_to_string(FuKineticDpFamily family);
FuKineticDpFamily
fu_kinetic_dp_chip_id_to_family(KtChipId chip_id);
