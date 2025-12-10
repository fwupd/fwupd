/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>
#include <xmlb.h>

#include "fu-common-struct.h"

/**
 * FU_BIT_SET:
 * @val: integer value
 * @pos: bit position, where 0 is the least significant byte
 *
 * Sets a bit in a value using a bitwise operation.
 *
 * Since: 2.0.0
 **/
#define FU_BIT_SET(val, pos) (val |= (1ull << (pos))) /* nocheck:blocked */

/**
 * FU_BIT_CLEAR:
 * @val: integer value
 * @pos: bit position, where 0 is the least significant byte
 *
 * Clears a bit in a value using a bitwise operation.
 *
 * Since: 2.0.0
 **/
#define FU_BIT_CLEAR(val, pos) (val &= ~(1ull << (pos))) /* nocheck:blocked */

/**
 * FU_BIT_IS_SET:
 * @val: integer value
 * @pos: bit position, where 0 is the least significant byte
 *
 * Checks a bit in a value using a bitwise operation.
 *
 * Returns: %TRUE if the bit is set.
 *
 * Since: 2.0.0
 **/
#define FU_BIT_IS_SET(val, pos) (val & (1ull << (pos)))

/**
 * FU_BIT_IS_CLEAR:
 * @val: integer value
 * @pos: bit position, where 0 is the least significant byte
 *
 * Checks a bit in a value using a bitwise operation.
 *
 * Returns: %TRUE if the bit is clear.
 *
 * Since: 2.0.0
 **/
#define FU_BIT_IS_CLEAR(val, pos) (!FU_BIT_IS_SET(val, (pos)))

gboolean
fu_cpuid(guint32 leaf, guint32 *eax, guint32 *ebx, guint32 *ecx, guint32 *edx, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
FuCpuVendor
fu_cpu_get_vendor(void);
GHashTable *
fu_cpu_get_attrs(GError **error);

guint64
fu_common_get_memory_size(void);
gchar *
fu_common_get_kernel_cmdline(GError **error);
gchar *
fu_common_get_olson_timezone_id(GError **error);

gsize
fu_common_align_up(gsize value, guint8 alignment);
gboolean
fu_power_state_is_ac(FuPowerState power_state);
gsize
fu_size_checked_add(gsize a, gsize b);

typedef struct {
	guint value;
	FwupdError code;
	const gchar *message;
} FuErrorMapEntry;

gboolean
fu_error_map_entry_to_gerror(guint value,
			     const FuErrorMapEntry entries[],
			     guint n_entries,
			     GError **error) G_GNUC_NON_NULL(2);

typedef struct {
	GQuark domain;
	gint code;
	FwupdError error;
} FuErrorConvertEntry;

gboolean
fu_error_convert(const FuErrorConvertEntry entries[], guint n_entries, GError **perror)
    G_GNUC_NON_NULL(1);

void
fu_xmlb_builder_insert_kv(XbBuilderNode *bn, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1);
void
fu_xmlb_builder_insert_kx(XbBuilderNode *bn, const gchar *key, guint64 value) G_GNUC_NON_NULL(1);
void
fu_xmlb_builder_insert_kb(XbBuilderNode *bn, const gchar *key, gboolean value) G_GNUC_NON_NULL(1);
