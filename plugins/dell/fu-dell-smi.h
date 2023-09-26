/*
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <smbios_c/obj/smi.h>
#include <smbios_c/smi.h>

typedef struct {
	struct dell_smi_obj *smi;
	guint32 input[4];
	guint32 output[4];
	gboolean fake_smbios;
	guint8 *fake_buffer;
} FuDellSmiObj;

gboolean
fu_dell_clear_smi(FuDellSmiObj *obj);

gboolean
fu_dell_execute_simple_smi(FuDellSmiObj *obj, guint16 class, guint16 select);

/* SMI return values used */
#define SMI_SUCCESS	   0
#define SMI_INVALID_BUFFER -6

/* These are DACI class/select needed for
 * flash capability queries
 */
#define DACI_FLASH_INTERFACE_CLASS  7
#define DACI_FLASH_INTERFACE_SELECT 3
#define DACI_FLASH_ARG_TPM	    2
