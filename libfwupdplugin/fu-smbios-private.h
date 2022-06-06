/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-smbios.h"

gboolean
fu_smbios_setup(FuSmbios *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_smbios_setup_from_path(FuSmbios *self,
			  const gchar *path,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_smbios_setup_from_file(FuSmbios *self,
			  const gchar *filename,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_smbios_setup_from_kernel(FuSmbios *self,
			    const gchar *path,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
