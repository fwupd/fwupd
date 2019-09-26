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
 * FuSynapticsmstMode:
 * @FU_SYNAPTICSMST_MODE_UNKNOWN:		Type invalid or not known
 * @FU_SYNAPTICSMST_MODE_DIRECT:		Directly addressable
 * @FU_SYNAPTICSMST_MODE_REMOTE:		Requires remote register work
 *
 * The device type.
 **/
typedef enum {
	FU_SYNAPTICSMST_MODE_UNKNOWN,
	FU_SYNAPTICSMST_MODE_DIRECT,
	FU_SYNAPTICSMST_MODE_REMOTE,
	/*< private >*/
	FU_SYNAPTICSMST_MODE_LAST
} FuSynapticsmstMode;

typedef enum {
	FU_SYNAPTICSMST_FAMILY_UNKNOWN,
	FU_SYNAPTICSMST_FAMILY_TESLA,
	FU_SYNAPTICSMST_FAMILY_LEAF,
	FU_SYNAPTICSMST_FAMILY_PANAMERA,
	/*<private >*/
	FU_SYNAPTICSMST_FAMILY_LAST
} FuSynapticsmstFamily;

const gchar		*fu_synapticsmst_mode_to_string		(FuSynapticsmstMode	 mode);
const gchar		*fu_synapticsmst_family_to_string	(FuSynapticsmstFamily	 family);
FuSynapticsmstFamily	 fu_synapticsmst_family_from_chip_id	(guint16		 chip_id);
