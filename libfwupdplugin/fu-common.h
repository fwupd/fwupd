/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>
#include <xmlb.h>

#include "fu-common-struct.h"

/**
 * FuEndianType:
 *
 * The endian type, e.g. %G_LITTLE_ENDIAN
 **/
typedef guint FuEndianType;

/**
 * FuCpuVendor:
 *
 * The CPU vendor.
 **/

/**
 * FuPowerState:
 *
 * The system power state.
 *
 * This does not have to be exactly what the battery is doing, but is supposed to represent the
 * 40,000ft view of the system power state.
 *
 * For example, it is perfectly correct to set %FU_POWER_STATE_AC if the system is connected to
 * AC power, but the battery cells are discharging for health or for other performance reasons.
 **/

/**
 * FuLidState:
 *
 * The device lid state.
 **/

/**
 * FuDisplayState:
 *
 * The device lid state.
 **/

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
gchar *
fu_common_get_olson_timezone_id(GError **error);

gsize
fu_common_align_up(gsize value, guint8 alignment);
gboolean
fu_power_state_is_ac(FuPowerState power_state);
void
fu_error_convert(GError **perror);

void
fu_xmlb_builder_insert_kv(XbBuilderNode *bn, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1);
void
fu_xmlb_builder_insert_kx(XbBuilderNode *bn, const gchar *key, guint64 value) G_GNUC_NON_NULL(1);
void
fu_xmlb_builder_insert_kb(XbBuilderNode *bn, const gchar *key, gboolean value) G_GNUC_NON_NULL(1);
