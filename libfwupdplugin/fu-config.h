/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_CONFIG (fu_config_get_type())
G_DECLARE_DERIVABLE_TYPE(FuConfig, fu_config, FU, CONFIG, GObject)

struct _FuConfigClass {
	GObjectClass parent_class;
};

void
fu_config_set_default(FuConfig *self, const gchar *section, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_config_set_value(FuConfig *self,
		    const gchar *section,
		    const gchar *key,
		    const gchar *value,
		    GError **error) G_GNUC_NON_NULL(1, 2);
gchar *
fu_config_get_value(FuConfig *self, const gchar *section, const gchar *key) G_GNUC_NON_NULL(1, 2);
gchar **
fu_config_get_value_strv(FuConfig *self, const gchar *section, const gchar *key)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_config_get_value_bool(FuConfig *self, const gchar *section, const gchar *key)
    G_GNUC_NON_NULL(1, 2);
guint64
fu_config_get_value_u64(FuConfig *self, const gchar *section, const gchar *key)
    G_GNUC_NON_NULL(1, 2);
