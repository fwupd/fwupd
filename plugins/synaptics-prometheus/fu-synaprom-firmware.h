/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPROM_FIRMWARE (fu_synaprom_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapromFirmware, fu_synaprom_firmware, FU, SYNAPROM_FIRMWARE, FuFirmware)

/* le */
typedef struct __attribute__((packed)) {
	guint32			 product;
	guint32			 id;			/* MFW unique id used for compat verification */
	guint32			 buildtime;		/* unix-style build time */
	guint32			 buildnum;		/* build number */
	guint8			 vmajor;		/* major version */
	guint8			 vminor;		/* minor version */
	guint8			 unused[6];
} FuSynapromFirmwareMfwHeader;

FuFirmware		*fu_synaprom_firmware_new	(void);
