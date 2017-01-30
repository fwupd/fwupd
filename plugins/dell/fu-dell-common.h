/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Mario Limonciello <mario_limonciello@dell.com>
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

#ifndef __FU_DELL_COMMON_H
#define __FU_DELL_COMMON_H

#include "fu-device.h"
#include <smbios_c/smi.h>
#include <smbios_c/obj/smi.h>
#include <efivar.h>

typedef struct {
	struct dell_smi_obj	*smi;
	guint32			input[4];
	guint32			output[4];
	gboolean		fake_smbios;
	guint8			*fake_buffer;
} FuDellSmiObj;

gboolean
fu_dell_clear_smi (FuDellSmiObj *obj);

guint32
fu_dell_get_res (FuDellSmiObj *smi_obj, guint8 arg);

gboolean
fu_dell_execute_smi (FuDellSmiObj *obj);

gboolean
fu_dell_execute_simple_smi (FuDellSmiObj *obj, guint16 class, guint16 select);

gboolean
fu_dell_detect_dock (FuDellSmiObj *obj, guint32 *location);

gboolean
fu_dell_toggle_flash (FuDevice *device, GError **error, gboolean enable);

/* SMI return values used */
#define SMI_SUCCESS			0
#define SMI_INVALID_BUFFER		-6

/* These are DACI class/select needed for
 * flash capability queries
 */
#define DACI_FLASH_INTERFACE_CLASS	7
#define DACI_FLASH_INTERFACE_SELECT	3
#define DACI_FLASH_ARG_TPM		2
#define DACI_FLASH_ARG_FLASH_MODE	3
#define DACI_FLASH_MODE_USER		0
#define DACI_FLASH_MODE_FLASH		1


/* DACI class/select for dock capabilities */
#define DACI_DOCK_CLASS			17
#define DACI_DOCK_SELECT		22
#define DACI_DOCK_ARG_COUNT		0
#define DACI_DOCK_ARG_INFO		1
#define DACI_DOCK_ARG_MODE		2
#define DACI_DOCK_ARG_MODE_USER		0
#define DACI_DOCK_ARG_MODE_FLASH	1

/* VID/PID of ethernet controller on dock */
#define DOCK_NIC_VID		0x0bda
#define DOCK_NIC_PID		0x8153

#endif /* __FU_DELL_COMMON_H */
