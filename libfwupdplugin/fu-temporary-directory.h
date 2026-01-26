/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_TEMPORARY_DIRECTORY (fu_temporary_directory_get_type())

G_DECLARE_FINAL_TYPE(FuTemporaryDirectory, fu_temporary_directory, FU, TEMPORARY_DIRECTORY, GObject)

FuTemporaryDirectory *
fu_temporary_directory_new(const gchar *prefix, GError **error);
const gchar *
fu_temporary_directory_get_path(FuTemporaryDirectory *self) G_GNUC_NON_NULL(1);
