/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Apollo Ling <apollo.ling@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#include "fu-synaptics-mst-struct.h"

#define SYNAPTICS_FLASH_MODE_DELAY 3 /* seconds */

FuSynapticsMstFamily
fu_synaptics_mst_family_from_chip_id(guint16 chip_id);
