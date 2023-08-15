/*
 * Copyright (C) 2023 Kate Hsuan <hpa@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include <fwupdplugin.h>

#include <fcntl.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#ifdef HAVE_GIO_UNIX
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#endif
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fu-console.h"
#include "fu-engine-security.h"
#include "fu-engine.h"
#include "fu-plugin-private.h"
#include "fu-security-attr-common.h"
#include "fu-security-attrs-private.h"
#include "fu-util-common.h"

static gboolean
fu_engine_security_fix(FuEngine *engine,
		       FuSecurityAttrs *attrs,
		       const gchar *appstream_id,
		       gboolean do_fix,
		       GError **error)
{
	FuPlugin *plugin;
	gboolean ret = FALSE;
	FwupdSecurityAttr *attr;
	g_autoptr(GHashTable) settings =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	attr = fu_security_attrs_get_by_appstream_id(attrs, appstream_id);
	if (attr == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Attribute was not found");
		return FALSE;
	}

	plugin = fu_engine_get_plugin_by_name(engine, fwupd_security_attr_get_plugin(attr), error);
	if (plugin) {
		if (!do_fix && fwupd_security_attr_get_bios_setting_id(attr) != NULL) {
			attr = fu_engine_get_previous_bios_security_attr(
			    engine,
			    appstream_id,
			    fwupd_security_attr_get_bios_setting_current_value(attr),
			    error);
		}
		if (do_fix)
			ret = fu_plugin_runner_security_fix(plugin, attr, error);
		else
			ret = fu_plugin_runner_security_unfix(plugin, attr, error);
	}

	if (ret) {
		return TRUE;
	} else if (g_error_matches(*error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_clear_error(error);
	} else {
		return FALSE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "Repair item is not supported.");
	return FALSE;
}

/**
 * fu_engine_security_harden:
 * @self: a #FuEngine
 * @appstream_id: the Appstream ID.
 * @do_fix: fix or unfix the security issue.
 * @error: (nullable): optional return location for an error
 *
 * This function is used to enable or disable the security fix.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_security_harden(FuEngine *engine,
			  const gchar *appstream_id,
			  gboolean do_fix,
			  GError **error)
{
	g_autoptr(GPtrArray) attrs_array = NULL;
	FuSecurityAttrs *attrs;

	/* for those BIOS fixes and unsupported items */
	attrs = fu_engine_get_host_security_attrs(engine);
	if (attrs == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Fail on getting security attributes.");
		return FALSE;
	}

	attrs_array = fu_security_attrs_get_all(attrs);

	for (guint i = 0; i < attrs_array->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(attrs_array, i);
		const gchar *appstream_tmp = fwupd_security_attr_get_appstream_id(attr);
		if (!g_strcmp0(appstream_id, appstream_tmp)) {
			return fu_engine_security_fix(engine, attrs, appstream_id, do_fix, error);
		}
	}

	/* for unknown Appstream IDs */
	g_set_error_literal(error,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "Repair item is not found.");
	return FALSE;
}
