/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <systemd/sd-journal.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

struct FuPluginData {
	gboolean		 has_started;
	gboolean		 has_tme;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(sd_journal, sd_journal_close)

static gboolean
fu_plugin_journald_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	const void *buf = NULL;
	gint rc;
	gsize bufsz = 0;
	sd_id128_t boot_id = { 0 };
	g_autoptr(sd_journal) j = NULL;

	/* already done */
	if (priv->has_started)
		return TRUE;

	/* read entire journal */
	rc = sd_journal_open (&j, SD_JOURNAL_SYSTEM | SD_JOURNAL_LOCAL_ONLY);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to open journal: %s [%i]",
			     strerror (rc), rc);
		return FALSE;
	}

	/* seek to current boot */
	rc = sd_id128_get_boot (&boot_id);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to get boot ID: %s [%i]",
			     strerror (rc), rc);
		return FALSE;
	}
	rc = sd_journal_seek_monotonic_usec(j, boot_id, 0);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to seek to boot ID: %s [%i]",
			     strerror (rc), rc);
		return FALSE;
	}

	/* read each entry */
	while (sd_journal_next (j) > 0) {
		rc = sd_journal_get_data (j, "_TRANSPORT", &buf, &bufsz);
		if (rc < 0)
			continue;
		if (g_strcmp0 ((const gchar *) buf + 11, "kernel") != 0)
			continue;
		rc = sd_journal_get_data (j, "MESSAGE", &buf, &bufsz);
		if (rc < 0)
			continue;
		if (g_getenv ("FWUPD_JOURNALD_VERBOSE") != NULL)
			g_debug ("%s", ((const gchar *) buf) + 8);
		if (g_strstr_len (buf, bufsz, "x86/tme: enabled by BIOS") != NULL)
			priv->has_tme = TRUE;
	}

	/* success */
	priv->has_started = TRUE;
	return TRUE;
}

static void
fu_plugin_add_security_attrs_tme (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fu_security_attrs_append (attrs, attr);
	if (priv->has_tme) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autoptr(GError) error = NULL;
	if (!fu_plugin_journald_startup (plugin, &error)) {
		g_warning ("failed to open journal: %s", error->message);
		return;
	}
	fu_plugin_add_security_attrs_tme (plugin, attrs);
}
