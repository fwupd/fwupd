/*
 * Copyright 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Apollo Ling <apollo.ling@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#include "fu-synaptics-mst-struct.h"

#define SYNAPTICS_FLASH_MODE_DELAY 3 /* seconds */

#define SYNAPTICS_IEEE_OUI 0x90CC24

FuSynapticsMstFamily
fu_synaptics_mst_family_from_chip_id(guint16 chip_id);
guint8
fu_synaptics_mst_calculate_crc8(guint8 crc, const guint8 *buf, gsize bufsz);
guint16
fu_synaptics_mst_calculate_crc16(guint16 crc, const guint8 *buf, gsize bufsz);
