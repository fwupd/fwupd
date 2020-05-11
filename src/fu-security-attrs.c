/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "fwupd-security-attr.h"

#include "fu-security-attrs.h"

gchar *
fu_security_attrs_calculate_hsi (GPtrArray *attrs)
{
	guint hsi_number = 0;
	FwupdSecurityAttrFlags flags = FWUPD_SECURITY_ATTR_FLAG_NONE;
	GString *str = g_string_new ("HSI:");
	const FwupdSecurityAttrFlags hpi_suffixes[] = {
		FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES,
		FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION,
		FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE,
		FWUPD_SECURITY_ATTR_FLAG_NONE,
	};

	/* find the highest HSI number where there are no failures and at least
	 * one success */
	for (guint j = 1; j <= FWUPD_SECURITY_ATTR_LEVEL_LAST; j++) {
		gboolean success_cnt = 0;
		gboolean failure_cnt = 0;
		for (guint i = 0; i < attrs->len; i++) {
			FwupdSecurityAttr *attr = g_ptr_array_index (attrs, i);
			if (fwupd_security_attr_get_level (attr) != j)
				continue;
			if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
				success_cnt++;
			else if (!fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED))
				failure_cnt++;
		}

		/* abort */
		if (failure_cnt > 0) {
			hsi_number = j - 1;
			break;
		}

		/* we matched at least one thing on this level */
		if (success_cnt > 0)
			hsi_number = j;
	}

	/* get a logical OR of the runtime flags */
	for (guint i = 0; i < attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index (attrs, i);
		if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED))
			continue;
		/* positive things */
		if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES) ||
		    fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION)) {
			if (!fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
				continue;
		}
		/* negative things */
		if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE)) {
			if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
				continue;
		}
		flags |= fwupd_security_attr_get_flags (attr);
	}

	g_string_append_printf (str, "%u", hsi_number);
	if (flags & (FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES |
		     FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION |
		     FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE)) {
		g_string_append (str, "+");
		for (guint j = 0; hpi_suffixes[j] != FWUPD_SECURITY_ATTR_FLAG_NONE; j++) {
			if (flags & hpi_suffixes[j])
				g_string_append (str, fwupd_security_attr_flag_to_suffix (hpi_suffixes[j]));
		}
	}
	return g_string_free (str, FALSE);
}

void
fu_security_attrs_depsolve (GPtrArray *attrs)
{
	g_autoptr(GHashTable) attrs_by_id = NULL;

	/* make hash of ID -> object */
	attrs_by_id = g_hash_table_new (g_str_hash, g_str_equal);
	for (guint i = 0; i < attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index (attrs, i);
		g_hash_table_insert (attrs_by_id,
				     (gpointer) fwupd_security_attr_get_appstream_id (attr),
				     (gpointer) attr);
	}

	/* set flat where required */
	for (guint i = 0; i < attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index (attrs, i);
		GPtrArray *obsoletes = fwupd_security_attr_get_obsoletes (attr);
		for (guint j = 0; j < obsoletes->len; j++) {
			const gchar *obsolete = g_ptr_array_index (obsoletes, j);
			FwupdSecurityAttr *attr_tmp = g_hash_table_lookup (attrs_by_id, obsolete);
			if (attr_tmp != NULL) {
				g_debug ("security attr %s obsoleted by %s", obsolete,
					 fwupd_security_attr_get_appstream_id (attr));
				fwupd_security_attr_add_flag (attr_tmp,
							      FWUPD_SECURITY_ATTR_FLAG_OBSOLETED);
			}
		}
	}
}
