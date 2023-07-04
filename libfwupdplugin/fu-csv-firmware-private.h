/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-csv-firmware.h"

guint
fu_csv_firmware_get_idx_for_column_id(FuCsvFirmware *self, const gchar *column_id);
