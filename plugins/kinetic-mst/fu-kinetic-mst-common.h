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

#define KINETIC_FLASH_MODE_DELAY	3	/* seconds */

/**
 * FuKineticMstMode:
 * @FU_KINETIC_MST_MODE_UNKNOWN:    Type invalid or not known
 * @FU_KINETIC_MST_MODE_DIRECT:     Directly addressable
 * @FU_KINETIC_MST_MODE_REMOTE:     Requires remote register work
 *
 * The device type.
 **/
typedef enum {
    FU_KINETIC_MST_MODE_UNKNOWN,
    FU_KINETIC_MST_MODE_DIRECT,
    //FU_KINETIC_MST_MODE_REMOTE,   // <TODO> Confirm if remote mode is needed?
    /*< private >*/
    FU_KINETIC_MST_MODE_LAST
} FuKineticMstMode;

/**
 * FuKineticMstFamily:
 * @FU_KINETIC_MST_FAMILY_UNKNOWN:  Family invalid or not known
 * @FU_KINETIC_MST_FAMILY_MUSTANG:  Mustang
 * @FU_KINETIC_MST_FAMILY_JAGUAR:   Jaguar
 *
 * The chip family.
 **/
typedef enum {
    FU_KINETIC_MST_FAMILY_UNKNOWN,

    FU_KINETIC_MST_FAMILY_MUSTANG,
    FU_KINETIC_MST_FAMILY_JAGUAR,
    /*<private >*/
    FU_KINETIC_MST_FAMILY_LAST
} FuKineticMstFamily;

const gchar *fu_kinetic_mst_mode_to_string(FuKineticMstMode	 mode);
const gchar *fu_kinetic_mst_family_to_string(FuKineticMstFamily	 family);
FuKineticMstFamily fu_kinetic_mst_family_from_chip_id(guint16 chip_id);

