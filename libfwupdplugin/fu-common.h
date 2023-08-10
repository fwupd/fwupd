/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <xmlb.h>

/**
 * FuEndianType:
 *
 * The endian type, e.g. %G_LITTLE_ENDIAN
 **/
typedef guint FuEndianType;

/**
 * FuCpuVendor:
 * @FU_CPU_VENDOR_UNKNOWN:		Unknown
 * @FU_CPU_VENDOR_INTEL:		Intel
 * @FU_CPU_VENDOR_AMD:			AMD
 *
 * The CPU vendor.
 **/
typedef enum {
	FU_CPU_VENDOR_UNKNOWN,
	FU_CPU_VENDOR_INTEL,
	FU_CPU_VENDOR_AMD,
	/*< private >*/
	FU_CPU_VENDOR_LAST
} FuCpuVendor;

/**
 * FuPowerState:
 * @FU_POWER_STATE_UNKNOWN:		Unknown
 * @FU_POWER_STATE_AC:			On AC power
 * @FU_POWER_STATE_AC_CHARGING:		Charging on AC
 * @FU_POWER_STATE_AC_FULLY_CHARGED:	Fully charged on AC
 * @FU_POWER_STATE_BATTERY:		On system battery
 * @FU_POWER_STATE_BATTERY_DISCHARGING:	System battery discharging
 * @FU_POWER_STATE_BATTERY_EMPTY:	System battery empty
 *
 * The system power state.
 *
 * This does not have to be exactly what the battery is doing, but is supposed to represent the
 * 40,000ft view of the system power state.
 *
 * For example, it is perfectly correct to set %FU_POWER_STATE_AC if the system is connected to
 * AC power, but the battery cells are discharging for health or for other performance reasons.
 **/
typedef enum {
	FU_POWER_STATE_UNKNOWN,
	FU_POWER_STATE_AC,
	FU_POWER_STATE_AC_CHARGING,
	FU_POWER_STATE_AC_FULLY_CHARGED,
	FU_POWER_STATE_BATTERY,
	FU_POWER_STATE_BATTERY_DISCHARGING,
	FU_POWER_STATE_BATTERY_EMPTY,
	/*< private >*/
	FU_POWER_STATE_LAST
} FuPowerState;

/**
 * FuLidState:
 * @FU_LID_STATE_UNKNOWN:		Unknown
 * @FU_LID_STATE_OPEN:			Charging
 * @FU_LID_STATE_CLOSED:		Discharging
 *
 * The device lid state.
 **/
typedef enum {
	FU_LID_STATE_UNKNOWN,
	FU_LID_STATE_OPEN,
	FU_LID_STATE_CLOSED,
	/*< private >*/
	FU_LID_STATE_LAST
} FuLidState;

gboolean
fu_cpuid(guint32 leaf, guint32 *eax, guint32 *ebx, guint32 *ecx, guint32 *edx, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
FuCpuVendor
fu_cpu_get_vendor(void);
gboolean
fu_common_is_live_media(void);
guint64
fu_common_get_memory_size(void);
gchar *
fu_common_get_kernel_cmdline(GError **error);
gboolean
fu_common_check_full_disk_encryption(GError **error);

gsize
fu_common_align_up(gsize value, guint8 alignment);
const gchar *
fu_power_state_to_string(FuPowerState power_state);
gboolean
fu_power_state_is_ac(FuPowerState power_state);
const gchar *
fu_lid_state_to_string(FuLidState lid_state);

void
fu_xmlb_builder_insert_kv(XbBuilderNode *bn, const gchar *key, const gchar *value);
void
fu_xmlb_builder_insert_kx(XbBuilderNode *bn, const gchar *key, guint64 value);
void
fu_xmlb_builder_insert_kb(XbBuilderNode *bn, const gchar *key, gboolean value);
