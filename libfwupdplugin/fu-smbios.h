/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-firmware.h"

#define FU_TYPE_SMBIOS (fu_smbios_get_type ())

G_DECLARE_FINAL_TYPE (FuSmbios, fu_smbios, FU, SMBIOS, FuFirmware)

FuSmbios	*fu_smbios_new			(void);

/**
 * FU_SMBIOS_STRUCTURE_TYPE_BIOS:
 *
 * The SMBIOS structure type for the BIOS.
 *
 * Since: 1.5.5
 **/
#define FU_SMBIOS_STRUCTURE_TYPE_BIOS		0x00
/**
 * FU_SMBIOS_STRUCTURE_TYPE_SYSTEM:
 *
 * The SMBIOS structure type for the system as a whole.
 *
 * Since: 1.5.5
 **/
#define FU_SMBIOS_STRUCTURE_TYPE_SYSTEM		0x01
/**
 * FU_SMBIOS_STRUCTURE_TYPE_BASEBOARD:
 *
 * The SMBIOS structure type for the baseboard (motherboard).
 *
 * Since: 1.5.5
 **/
#define FU_SMBIOS_STRUCTURE_TYPE_BASEBOARD	0x02
/**
 * FU_SMBIOS_STRUCTURE_TYPE_CHASSIS:
 *
 * The SMBIOS structure type for the chassis.
 *
 * Since: 1.5.5
 **/
#define FU_SMBIOS_STRUCTURE_TYPE_CHASSIS	0x03
/**
 * FU_SMBIOS_STRUCTURE_TYPE_LAST:
 *
 * The last possible SMBIOS structure type.
 *
 * Since: 1.5.5
 **/
#define FU_SMBIOS_STRUCTURE_TYPE_LAST		0x04

/**
 * FuSmbiosChassisKind:
 *
 * The system chassis kind.
 **/
typedef enum {
	FU_SMBIOS_CHASSIS_KIND_OTHER		= 0x01,
	FU_SMBIOS_CHASSIS_KIND_UNKNOWN		= 0x02,
	FU_SMBIOS_CHASSIS_KIND_DESKTOP		= 0x03,
	FU_SMBIOS_CHASSIS_KIND_LOW_PROFILE_DESKTOP = 0x04,
	FU_SMBIOS_CHASSIS_KIND_PIZZA_BOX	= 0x05,
	FU_SMBIOS_CHASSIS_KIND_MINI_TOWER	= 0x06,
	FU_SMBIOS_CHASSIS_KIND_TOWER		= 0x07,
	FU_SMBIOS_CHASSIS_KIND_PORTABLE		= 0x08,
	FU_SMBIOS_CHASSIS_KIND_LAPTOP		= 0x09,
	FU_SMBIOS_CHASSIS_KIND_NOTEBOOK		= 0x0A,
	FU_SMBIOS_CHASSIS_KIND_HAND_HELD	= 0x0B,
	FU_SMBIOS_CHASSIS_KIND_DOCKING_STATION	= 0x0C,
	FU_SMBIOS_CHASSIS_KIND_ALL_IN_ONE	= 0x0D,
	FU_SMBIOS_CHASSIS_KIND_SUB_NOTEBOOK	= 0x0E,
	FU_SMBIOS_CHASSIS_KIND_SPACE_SAVING	= 0x0F,
	FU_SMBIOS_CHASSIS_KIND_LUNCH_BOX	= 0x10,
	FU_SMBIOS_CHASSIS_KIND_MAIN_SERVER	= 0x11,
	FU_SMBIOS_CHASSIS_KIND_EXPANSION	= 0x12,
	FU_SMBIOS_CHASSIS_KIND_SUBCHASSIS	= 0x13,
	FU_SMBIOS_CHASSIS_KIND_BUS_EXPANSION	= 0x14,
	FU_SMBIOS_CHASSIS_KIND_PERIPHERAL	= 0x15,
	FU_SMBIOS_CHASSIS_KIND_RAID		= 0x16,
	FU_SMBIOS_CHASSIS_KIND_RACK_MOUNT	= 0x17,
	FU_SMBIOS_CHASSIS_KIND_SEALED_CASE_PC	= 0x18,
	FU_SMBIOS_CHASSIS_KIND_MULTI_SYSTEM	= 0x19,
	FU_SMBIOS_CHASSIS_KIND_COMPACT_PCI	= 0x1A,
	FU_SMBIOS_CHASSIS_KIND_ADVANCED_TCA	= 0x1B,
	FU_SMBIOS_CHASSIS_KIND_BLADE		= 0x1C,
	FU_SMBIOS_CHASSIS_KIND_TABLET		= 0x1E,
	FU_SMBIOS_CHASSIS_KIND_CONVERTIBLE	= 0x1F,
	FU_SMBIOS_CHASSIS_KIND_DETACHABLE	= 0x20,
	FU_SMBIOS_CHASSIS_KIND_IOT_GATEWAY	= 0x21,
	FU_SMBIOS_CHASSIS_KIND_EMBEDDED_PC	= 0x22,
	FU_SMBIOS_CHASSIS_KIND_MINI_PC		= 0x23,
	FU_SMBIOS_CHASSIS_KIND_STICK_PC		= 0x24,
	/*< private >*/
	FU_SMBIOS_CHASSIS_KIND_LAST,
} FuSmbiosChassisKind;

gchar		*fu_smbios_to_string		(FuSmbios	*self);

const gchar	*fu_smbios_get_string		(FuSmbios	*self,
						 guint8		 type,
						 guint8		 offset,
						 GError		**error);
guint		 fu_smbios_get_integer		(FuSmbios	*self,
						 guint8		 type,
						 guint8		 offset,
						 GError		**error);
GBytes		*fu_smbios_get_data		(FuSmbios	*self,
						 guint8		 type,
						 GError		**error);
