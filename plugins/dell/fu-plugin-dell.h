/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Mario Limonciello <mario_limonciello@dell.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_PLUGIN_DELL_H
#define __FU_PLUGIN_DELL_H

#include <gusb.h>
#include "fu-plugin.h"
#include "fu-dell-common.h"
void
fu_plugin_dell_inject_fake_data (FuPlugin *plugin,
				   guint32 *output, guint16 vid, guint16 pid,
				   guint8 *buf);

gboolean
fu_plugin_dell_detect_tpm (FuPlugin *plugin, GError **error);

void
fu_plugin_dell_device_added_cb (GUsbContext *ctx,
				  GUsbDevice *device,
				  FuPlugin *plugin);

void
fu_plugin_dell_device_removed_cb (GUsbContext *ctx,
				    GUsbDevice *device,
				    FuPlugin *plugin);

/* These are nodes that will indicate information about
 * the TPM status
 */
struct tpm_status {
	guint32 ret;
	guint32 fw_version;
	guint32 status;
	guint32 flashes_left;
};
#define TPM_EN_MASK	0x0001
#define TPM_OWN_MASK	0x0004
#define TPM_TYPE_MASK	0x0F00
#define TPM_1_2_MODE	0x0001
#define TPM_2_0_MODE	0x0002

/* Dock Info version 1 */
#pragma pack(1)
#define MAX_COMPONENTS 5

typedef struct _COMPONENTS {
	gchar		description[80];
	guint32		fw_version; 		/* BCD format: 0x00XXYYZZ */
} COMPONENTS;

typedef struct _DOCK_INFO {
	gchar		dock_description[80];
	guint32		flash_pkg_version;	/* BCD format: 0x00XXYYZZ */
	guint32		cable_type;		/* bit0-7 cable type, bit7-31 set to 0 */
	guint8		location;		/* Location of the dock */
	guint8		reserved;
	guint8		component_count;
	COMPONENTS	components[MAX_COMPONENTS];	/* number of component_count */
} DOCK_INFO;

typedef struct _DOCK_INFO_HEADER {
	guint8		dir_version;  		/* version 1, 2 … */
	guint8		dock_type;
	guint16		reserved;
} DOCK_INFO_HEADER;

typedef struct _DOCK_INFO_RECORD {
	DOCK_INFO_HEADER	dock_info_header; /* dock version specific definition */
	DOCK_INFO		dock_info;
} DOCK_INFO_RECORD;

typedef union _INFO_UNION{
	guint8 *buf;
	DOCK_INFO_RECORD *record;
} INFO_UNION;
#pragma pack()

typedef enum _DOCK_TYPE
{
	DOCK_TYPE_NONE,
	DOCK_TYPE_TB15,
	DOCK_TYPE_WD15
} DOCK_TYPE;

typedef enum _CABLE_TYPE
{
	CABLE_TYPE_NONE,
	CABLE_TYPE_LEGACY,
	CABLE_TYPE_UNIV,
	CABLE_TYPE_TBT
} CABLE_TYPE;

#endif /* __FU_PLUGIN_DELL_H */
