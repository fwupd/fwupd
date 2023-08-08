/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_CSV_FIRMWARE (fu_csv_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCsvFirmware, fu_csv_firmware, FU, CSV_FIRMWARE, FuFirmware)

struct _FuCsvFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_csv_firmware_new(void);
void
fu_csv_firmware_add_column_id(FuCsvFirmware *self, const gchar *column_id);
const gchar *
fu_csv_firmware_get_column_id(FuCsvFirmware *self, guint idx);
