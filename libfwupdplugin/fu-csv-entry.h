/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_CSV_ENTRY (fu_csv_entry_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCsvEntry, fu_csv_entry, FU, CSV_ENTRY, FuFirmware)

struct _FuCsvEntryClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_csv_entry_new(void);
void
fu_csv_entry_add_value(FuCsvEntry *self, const gchar *value);
const gchar *
fu_csv_entry_get_value_by_idx(FuCsvEntry *self, guint idx);
const gchar *
fu_csv_entry_get_value_by_column_id(FuCsvEntry *self, const gchar *column_id);
gboolean
fu_csv_entry_get_value_by_column_id_uint64(FuCsvEntry *self,
					   const gchar *column_id,
					   guint64 *value,
					   GError **error);
