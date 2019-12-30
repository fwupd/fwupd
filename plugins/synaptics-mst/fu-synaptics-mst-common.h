/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#define SYNAPTICS_FLASH_MODE_DELAY	3	/* seconds */

/**
 * FuSynapticsMstMode:
 * @FU_SYNAPTICS_MST_MODE_UNKNOWN:		Type invalid or not known
 * @FU_SYNAPTICS_MST_MODE_DIRECT:		Directly addressable
 * @FU_SYNAPTICS_MST_MODE_REMOTE:		Requires remote register work
 *
 * The device type.
 **/
typedef enum {
	FU_SYNAPTICS_MST_MODE_UNKNOWN,
	FU_SYNAPTICS_MST_MODE_DIRECT,
	FU_SYNAPTICS_MST_MODE_REMOTE,
	/*< private >*/
	FU_SYNAPTICS_MST_MODE_LAST
} FuSynapticsMstMode;

typedef enum {
	FU_SYNAPTICS_MST_FAMILY_UNKNOWN,
	FU_SYNAPTICS_MST_FAMILY_TESLA,
	FU_SYNAPTICS_MST_FAMILY_LEAF,
	FU_SYNAPTICS_MST_FAMILY_PANAMERA,
	/*<private >*/
	FU_SYNAPTICS_MST_FAMILY_LAST
} FuSynapticsMstFamily;

const gchar		*fu_synaptics_mst_mode_to_string		(FuSynapticsMstMode	 mode);
const gchar		*fu_synaptics_mst_family_to_string	(FuSynapticsMstFamily	 family);
FuSynapticsMstFamily	 fu_synaptics_mst_family_from_chip_id	(guint16		 chip_id);
