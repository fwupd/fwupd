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
 * FuBatteryState:
 * @FU_BATTERY_STATE_UNKNOWN:		Unknown
 * @FU_BATTERY_STATE_CHARGING:		Charging
 * @FU_BATTERY_STATE_DISCHARGING:	Discharging
 * @FU_BATTERY_STATE_EMPTY:		Empty
 * @FU_BATTERY_STATE_FULLY_CHARGED:	Fully charged
 *
 * The device battery state.
 **/
typedef enum {
	FU_BATTERY_STATE_UNKNOWN,
	FU_BATTERY_STATE_CHARGING,
	FU_BATTERY_STATE_DISCHARGING,
	FU_BATTERY_STATE_EMPTY,
	FU_BATTERY_STATE_FULLY_CHARGED,
	/*< private >*/
	FU_BATTERY_STATE_LAST
} FuBatteryState;

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
gboolean
fu_common_check_full_disk_encryption(GError **error);

gsize
fu_common_align_up(gsize value, guint8 alignment);
const gchar *
fu_battery_state_to_string(FuBatteryState battery_state);
const gchar *
fu_lid_state_to_string(FuLidState lid_state);

void
fu_xmlb_builder_insert_kv(XbBuilderNode *bn, const gchar *key, const gchar *value);
void
fu_xmlb_builder_insert_kx(XbBuilderNode *bn, const gchar *key, guint64 value);
void
fu_xmlb_builder_insert_kb(XbBuilderNode *bn, const gchar *key, gboolean value);
