/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-smbios.h"

gboolean	 fu_smbios_setup		(FuSmbios	*self,
						 GError		**error);
gboolean	 fu_smbios_setup_from_path	(FuSmbios	*self,
						 const gchar	*path,
						 GError		**error);
gboolean	 fu_smbios_setup_from_file	(FuSmbios	*self,
						 const gchar	*filename,
						 GError		**error);
