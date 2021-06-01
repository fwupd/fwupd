/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-efivar.h"

gboolean	 fu_efivar_supported_impl	(GError		**error);
guint64		 fu_efivar_space_used_impl	(GError		**error);
gboolean	 fu_efivar_exists_impl		(const gchar	*guid,
						 const gchar	*name);
GFileMonitor	*fu_efivar_get_monitor_impl	(const gchar	*guid,
						 const gchar	*name,
						 GError		**error);
gboolean	 fu_efivar_get_data_impl	(const gchar	*guid,
						 const gchar	*name,
						 guint8		**data,
						 gsize		*data_sz,
						 guint32	*attr,
						 GError		**error);
gboolean	 fu_efivar_set_data_impl	(const gchar	*guid,
						 const gchar	*name,
						 const guint8	*data,
						 gsize		 sz,
						 guint32	 attr,
						 GError		**error);
gboolean	 fu_efivar_delete_impl		(const gchar	*guid,
						 const gchar	*name,
						 GError		**error);
gboolean	 fu_efivar_delete_with_glob_impl(const gchar	*guid,
						 const gchar	*name_glob,
						 GError		**error);
GPtrArray	*fu_efivar_get_names_impl	(const gchar	*guid,
						 GError		**error);
