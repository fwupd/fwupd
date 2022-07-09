/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fwupd-common-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-security-attr-private.h"

/**
 * FwupdSecurityAttr:
 *
 * A Host Security ID attribute that represents something that was measured.
 */

static void
fwupd_security_attr_finalize(GObject *object);

typedef struct {
	gchar *appstream_id;
	GPtrArray *obsoletes;
	GPtrArray *guids;
	GHashTable *metadata; /* (nullable) */
	gchar *name;
	gchar *title;
	gchar *description;
	gchar *plugin;
	gchar *url;
	guint64 created;
	FwupdSecurityAttrLevel level;
	FwupdSecurityAttrResult result;
	FwupdSecurityAttrResult result_fallback;
	FwupdSecurityAttrFlags flags;
} FwupdSecurityAttrPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FwupdSecurityAttr, fwupd_security_attr, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_security_attr_get_instance_private(o))

/**
 * fwupd_security_attr_flag_to_string:
 * @flag: security attribute flags, e.g. %FWUPD_SECURITY_ATTR_FLAG_SUCCESS
 *
 * Returns the printable string for the flag.
 *
 * Returns: string, or %NULL
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_flag_to_string(FwupdSecurityAttrFlags flag)
{
	if (flag == FWUPD_SECURITY_ATTR_FLAG_NONE)
		return "none";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_SUCCESS)
		return "success";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)
		return "obsoleted";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA)
		return "missing-data";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES)
		return "runtime-updates";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION)
		return "runtime-attestation";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE)
		return "runtime-issue";
	return NULL;
}

/**
 * fwupd_security_attr_flag_from_string:
 * @flag: (nullable): a string, e.g. `success`
 *
 * Converts a string to an enumerated flag.
 *
 * Returns: enumerated value
 *
 * Since: 1.7.1
 **/
FwupdSecurityAttrFlags
fwupd_security_attr_flag_from_string(const gchar *flag)
{
	if (g_strcmp0(flag, "success") == 0)
		return FWUPD_SECURITY_ATTR_FLAG_SUCCESS;
	if (g_strcmp0(flag, "obsoleted") == 0)
		return FWUPD_SECURITY_ATTR_FLAG_OBSOLETED;
	if (g_strcmp0(flag, "missing-data") == 0)
		return FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA;
	if (g_strcmp0(flag, "runtime-updates") == 0)
		return FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES;
	if (g_strcmp0(flag, "runtime-attestation") == 0)
		return FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION;
	if (g_strcmp0(flag, "runtime-issue") == 0)
		return FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE;
	return FWUPD_SECURITY_ATTR_FLAG_NONE;
}

/**
 * fwupd_security_attr_result_to_string:
 * @result: security attribute result, e.g. %FWUPD_SECURITY_ATTR_RESULT_ENABLED
 *
 * Returns the printable string for the result enum.
 *
 * Returns: string, or %NULL
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_result_to_string(FwupdSecurityAttrResult result)
{
	if (result == FWUPD_SECURITY_ATTR_RESULT_VALID)
		return "valid";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_VALID)
		return "not-valid";
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENABLED)
		return "enabled";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED)
		return "not-enabled";
	if (result == FWUPD_SECURITY_ATTR_RESULT_LOCKED)
		return "locked";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED)
		return "not-locked";
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED)
		return "encrypted";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED)
		return "not-encrypted";
	if (result == FWUPD_SECURITY_ATTR_RESULT_TAINTED)
		return "tainted";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED)
		return "not-tainted";
	if (result == FWUPD_SECURITY_ATTR_RESULT_FOUND)
		return "found";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND)
		return "not-found";
	if (result == FWUPD_SECURITY_ATTR_RESULT_SUPPORTED)
		return "supported";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED)
		return "not-supported";
	return NULL;
}

/**
 * fwupd_security_attr_result_from_string:
 * @result: (nullable): a string, e.g. `not-encrypted`
 *
 * Converts a string to an enumerated result.
 *
 * Returns: enumerated value
 *
 * Since: 1.7.1
 **/
FwupdSecurityAttrResult
fwupd_security_attr_result_from_string(const gchar *result)
{
	if (g_strcmp0(result, "valid") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_VALID;
	if (g_strcmp0(result, "not-valid") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_NOT_VALID;
	if (g_strcmp0(result, "enabled") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_ENABLED;
	if (g_strcmp0(result, "not-enabled") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED;
	if (g_strcmp0(result, "locked") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_LOCKED;
	if (g_strcmp0(result, "not-locked") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED;
	if (g_strcmp0(result, "encrypted") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED;
	if (g_strcmp0(result, "not-encrypted") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED;
	if (g_strcmp0(result, "tainted") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_TAINTED;
	if (g_strcmp0(result, "not-tainted") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED;
	if (g_strcmp0(result, "found") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_FOUND;
	if (g_strcmp0(result, "not-found") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND;
	if (g_strcmp0(result, "supported") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_SUPPORTED;
	if (g_strcmp0(result, "not-supported") == 0)
		return FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED;
	return FWUPD_SECURITY_ATTR_RESULT_UNKNOWN;
}

/**
 * fwupd_security_attr_flag_to_suffix:
 * @flag: security attribute flags, e.g. %FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES
 *
 * Returns the string suffix for the flag.
 *
 * Returns: string, or %NULL
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_flag_to_suffix(FwupdSecurityAttrFlags flag)
{
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES)
		return "U";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION)
		return "A";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE)
		return "!";
	return NULL;
}

/**
 * fwupd_security_attr_get_obsoletes:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the list of attribute obsoletes. The obsoleted attributes will not
 * contribute to the calculated HSI value or be visible in command line tools.
 *
 * Returns: (element-type utf8) (transfer none): the obsoletes, which may be empty
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_security_attr_get_obsoletes(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);
	return priv->obsoletes;
}

/**
 * fwupd_security_attr_add_obsolete:
 * @self: a #FwupdSecurityAttr
 * @appstream_id: the appstream_id or plugin name
 *
 * Adds an attribute appstream_id to obsolete. The obsoleted attribute will not
 * contribute to the calculated HSI value or be visible in command line tools.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_add_obsolete(FwupdSecurityAttr *self, const gchar *appstream_id)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	g_return_if_fail(appstream_id != NULL);
	if (fwupd_security_attr_has_obsolete(self, appstream_id))
		return;
	g_ptr_array_add(priv->obsoletes, g_strdup(appstream_id));
}

/**
 * fwupd_security_attr_has_obsolete:
 * @self: a #FwupdSecurityAttr
 * @appstream_id: the attribute appstream_id
 *
 * Finds out if the attribute obsoletes a specific appstream_id.
 *
 * Returns: %TRUE if the self matches
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_security_attr_has_obsolete(FwupdSecurityAttr *self, const gchar *appstream_id)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), FALSE);
	g_return_val_if_fail(appstream_id != NULL, FALSE);
	for (guint i = 0; i < priv->obsoletes->len; i++) {
		const gchar *obsolete_tmp = g_ptr_array_index(priv->obsoletes, i);
		if (g_strcmp0(obsolete_tmp, appstream_id) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_security_attr_get_guids:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the list of attribute GUIDs. The GUID values will not modify the calculated HSI value.
 *
 * Returns: (element-type utf8) (transfer none): the GUIDs, which may be empty
 *
 * Since: 1.7.0
 **/
GPtrArray *
fwupd_security_attr_get_guids(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);
	return priv->guids;
}

/**
 * fwupd_security_attr_add_guid:
 * @self: a #FwupdSecurityAttr
 * @guid: the GUID
 *
 * Adds a device GUID to the attribute. This indicates the GUID in some way contributed to the
 * result decided.
 *
 * Since: 1.7.0
 **/
void
fwupd_security_attr_add_guid(FwupdSecurityAttr *self, const gchar *guid)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	g_return_if_fail(fwupd_guid_is_valid(guid));
	if (fwupd_security_attr_has_guid(self, guid))
		return;
	g_ptr_array_add(priv->guids, g_strdup(guid));
}

/**
 * fwupd_security_attr_add_guids:
 * @self: a #FwupdSecurityAttr
 * @guids: (element-type utf8): the GUIDs
 *
 * Adds device GUIDs to the attribute. This indicates the GUIDs in some way contributed to the
 * result decided.
 *
 * Since: 1.7.0
 **/
void
fwupd_security_attr_add_guids(FwupdSecurityAttr *self, GPtrArray *guids)
{
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	g_return_if_fail(guids != NULL);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		fwupd_security_attr_add_guid(self, guid);
	}
}

/**
 * fwupd_security_attr_has_guid:
 * @self: a #FwupdSecurityAttr
 * @guid: the attribute guid
 *
 * Finds out if a specific GUID was added to the attribute.
 *
 * Returns: %TRUE if the self matches
 *
 * Since: 1.7.0
 **/
gboolean
fwupd_security_attr_has_guid(FwupdSecurityAttr *self, const gchar *guid)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	for (guint i = 0; i < priv->guids->len; i++) {
		const gchar *guid_tmp = g_ptr_array_index(priv->guids, i);
		if (g_strcmp0(guid_tmp, guid) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_security_attr_get_appstream_id:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the AppStream ID.
 *
 * Returns: the AppStream ID, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_appstream_id(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);
	return priv->appstream_id;
}

/**
 * fwupd_security_attr_set_appstream_id:
 * @self: a #FwupdSecurityAttr
 * @appstream_id: (nullable): the AppStream component ID, e.g. `com.intel.BiosGuard`
 *
 * Sets the AppStream ID.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_appstream_id(FwupdSecurityAttr *self, const gchar *appstream_id)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));

	/* not changed */
	if (g_strcmp0(priv->appstream_id, appstream_id) == 0)
		return;

	/* sanity check */
	if (!g_str_has_prefix(appstream_id, "org.fwupd.hsi."))
		g_critical("HSI attributes need to have a 'org.fwupd.hsi.' prefix");

	g_free(priv->appstream_id);
	priv->appstream_id = g_strdup(appstream_id);
}

/**
 * fwupd_security_attr_get_url:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the attribute URL.
 *
 * Returns: the attribute result, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_url(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);
	return priv->url;
}

/**
 * fwupd_security_attr_set_name:
 * @self: a #FwupdSecurityAttr
 * @name: (nullable): the attribute name
 *
 * Sets the attribute name.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_name(FwupdSecurityAttr *self, const gchar *name)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));

	/* not changed */
	if (g_strcmp0(priv->name, name) == 0)
		return;

	g_free(priv->name);
	priv->name = g_strdup(name);
}

/**
 * fwupd_security_attr_set_title:
 * @self: a #FwupdSecurityAttr
 * @title: (nullable): the attribute title
 *
 * Sets the attribute title.
 *
 * Since: 1.8.2
 **/
void
fwupd_security_attr_set_title(FwupdSecurityAttr *self, const gchar *title)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));

	/* not changed */
	if (g_strcmp0(priv->title, title) == 0)
		return;

	g_free(priv->title);
	priv->title = g_strdup(title);
}

/**
 * fwupd_security_attr_set_description:
 * @self: a #FwupdSecurityAttr
 * @description: (nullable): the attribute description
 *
 * Sets the attribute description.
 *
 * Since: 1.8.2
 **/
void
fwupd_security_attr_set_description(FwupdSecurityAttr *self, const gchar *description)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));

	/* not changed */
	if (g_strcmp0(priv->description, description) == 0)
		return;

	g_free(priv->description);
	priv->description = g_strdup(description);
}

/**
 * fwupd_security_attr_set_plugin:
 * @self: a #FwupdSecurityAttr
 * @plugin: (nullable): the plugin name
 *
 * Sets the plugin that created the attribute.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_plugin(FwupdSecurityAttr *self, const gchar *plugin)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));

	/* not changed */
	if (g_strcmp0(priv->plugin, plugin) == 0)
		return;

	g_free(priv->plugin);
	priv->plugin = g_strdup(plugin);
}

/**
 * fwupd_security_attr_set_url:
 * @self: a #FwupdSecurityAttr
 * @url: (nullable): the attribute URL
 *
 * Sets the attribute result.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_url(FwupdSecurityAttr *self, const gchar *url)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));

	/* not changed */
	if (g_strcmp0(priv->url, url) == 0)
		return;

	g_free(priv->url);
	priv->url = g_strdup(url);
}
/**
 * fwupd_security_attr_get_created:
 * @self: a #FwupdSecurityAttr
 *
 * Gets when the attribute was created.
 *
 * Returns: the UNIX time, or 0 if unset
 *
 * Since: 1.7.1
 **/
guint64
fwupd_security_attr_get_created(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), 0);
	return priv->created;
}

/**
 * fwupd_security_attr_set_created:
 * @self: a #FwupdSecurityAttr
 * @created: the UNIX time
 *
 * Sets when the attribute was created.
 *
 * Since: 1.7.1
 **/
void
fwupd_security_attr_set_created(FwupdSecurityAttr *self, guint64 created)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	priv->created = created;
}

/**
 * fwupd_security_attr_get_name:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the attribute name.
 *
 * Returns: the attribute name, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_name(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);
	return priv->name;
}

/**
 * fwupd_security_attr_get_title:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the attribute title, which is typically a two word title.
 *
 * The fwupd client program may be able to get translations for this value using a method call
 * like `dgettext("fwupd",str)`.
 *
 * Returns: the attribute title, or %NULL if unset
 *
 * Since: 1.8.2
 **/
const gchar *
fwupd_security_attr_get_title(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);
	return priv->title;
}

/**
 * fwupd_security_attr_get_description:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the attribute description which is a few lines of prose that normal users will understand.
 *
 * The fwupd client program may be able to get translations for this value using a method call
 * like `dgettext("fwupd",str)`.
 *
 * NOTE: The returned string may contain placeholders such as `$HostVendor$` or `$HostProduct$`
 * and these should be replaced with the values from [method@FwupdClient.get_host_vendor] and
 * [method@FwupdClient.get_host_product].
 *
 * Returns: the attribute description, or %NULL if unset
 *
 * Since: 1.8.2
 **/
const gchar *
fwupd_security_attr_get_description(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);
	return priv->description;
}

/**
 * fwupd_security_attr_get_plugin:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the plugin that created the attribute.
 *
 * Returns: the plugin name, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_plugin(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);
	return priv->plugin;
}

/**
 * fwupd_security_attr_get_flags:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the self flags.
 *
 * Returns: security attribute flags, or 0 if unset
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttrFlags
fwupd_security_attr_get_flags(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), 0);
	return priv->flags;
}

/**
 * fwupd_security_attr_set_flags:
 * @self: a #FwupdSecurityAttr
 * @flags: security attribute flags, e.g. %FWUPD_SECURITY_ATTR_FLAG_OBSOLETED
 *
 * Sets the attribute flags.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_flags(FwupdSecurityAttr *self, FwupdSecurityAttrFlags flags)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	priv->flags = flags;
}

/**
 * fwupd_security_attr_add_flag:
 * @self: a #FwupdSecurityAttr
 * @flag: the #FwupdSecurityAttrFlags, e.g. %FWUPD_SECURITY_ATTR_FLAG_OBSOLETED
 *
 * Adds a specific attribute flag to the attribute.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_add_flag(FwupdSecurityAttr *self, FwupdSecurityAttrFlags flag)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	priv->flags |= flag;
}

/**
 * fwupd_security_attr_has_flag:
 * @self: a #FwupdSecurityAttr
 * @flag: the attribute flag, e.g. %FWUPD_SECURITY_ATTR_FLAG_OBSOLETED
 *
 * Finds if the attribute has a specific attribute flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_security_attr_has_flag(FwupdSecurityAttr *self, FwupdSecurityAttrFlags flag)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * fwupd_security_attr_get_level:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the HSI level.
 *
 * Returns: the security attribute level, or %FWUPD_SECURITY_ATTR_LEVEL_NONE if unset
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttrLevel
fwupd_security_attr_get_level(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), 0);
	return priv->level;
}

/**
 * fwupd_security_attr_set_level:
 * @self: a #FwupdSecurityAttr
 * @level: a security attribute level, e.g. %FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT
 *
 * Sets the HSI level. A @level of %FWUPD_SECURITY_ATTR_LEVEL_NONE is not used
 * for the HSI calculation.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_level(FwupdSecurityAttr *self, FwupdSecurityAttrLevel level)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	priv->level = level;
}

/**
 * fwupd_security_attr_set_result:
 * @self: a #FwupdSecurityAttr
 * @result: a security attribute result, e.g. %FWUPD_SECURITY_ATTR_LEVEL_LOCKED
 *
 * Sets the optional HSI result. This is required because some attributes may
 * be a "success" when something is `locked` or may be "failed" if `found`.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_result(FwupdSecurityAttr *self, FwupdSecurityAttrResult result)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	priv->result = result;
}

/**
 * fwupd_security_attr_get_result:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the optional HSI result.
 *
 * Returns: the #FwupdSecurityAttrResult, e.g %FWUPD_SECURITY_ATTR_LEVEL_LOCKED
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttrResult
fwupd_security_attr_get_result(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), 0);
	return priv->result;
}

/**
 * fwupd_security_attr_set_result_fallback:
 * @self: a #FwupdSecurityAttr
 * @result: a security attribute, e.g. %FWUPD_SECURITY_ATTR_LEVEL_LOCKED
 *
 * Sets the optional fallback HSI result. The fallback may represent the old state, or a state
 * that may be considered equivalent.
 *
 * Since: 1.7.1
 **/
void
fwupd_security_attr_set_result_fallback(FwupdSecurityAttr *self, FwupdSecurityAttrResult result)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	priv->result_fallback = result;
}

/**
 * fwupd_security_attr_get_result_fallback:
 * @self: a #FwupdSecurityAttr
 *
 * Gets the optional fallback HSI result.
 *
 * Returns: the #FwupdSecurityAttrResult, e.g %FWUPD_SECURITY_ATTR_LEVEL_LOCKED
 *
 * Since: 1.7.1
 **/
FwupdSecurityAttrResult
fwupd_security_attr_get_result_fallback(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), 0);
	return priv->result_fallback;
}

/**
 * fwupd_security_attr_to_variant:
 * @self: a #FwupdSecurityAttr
 *
 * Serialize the security attribute.
 *
 * Returns: the serialized data, or %NULL for error
 *
 * Since: 1.5.0
 **/
GVariant *
fwupd_security_attr_to_variant(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;

	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);

	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	if (priv->appstream_id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_APPSTREAM_ID,
				      g_variant_new_string(priv->appstream_id));
	}
	if (priv->created > 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CREATED,
				      g_variant_new_uint64(priv->created));
	}
	if (priv->name != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_NAME,
				      g_variant_new_string(priv->name));
	}
	if (priv->title != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_SUMMARY,
				      g_variant_new_string(priv->title));
	}
	if (priv->description != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DESCRIPTION,
				      g_variant_new_string(priv->description));
	}
	if (priv->plugin != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_PLUGIN,
				      g_variant_new_string(priv->plugin));
	}
	if (priv->url != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_URI,
				      g_variant_new_string(priv->url));
	}
	if (priv->obsoletes->len > 0) {
		g_autofree const gchar **strv = g_new0(const gchar *, priv->obsoletes->len + 1);
		for (guint i = 0; i < priv->obsoletes->len; i++)
			strv[i] = (const gchar *)g_ptr_array_index(priv->obsoletes, i);
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CATEGORIES,
				      g_variant_new_strv(strv, -1));
	}
	if (priv->guids->len > 0) {
		g_autofree const gchar **strv = g_new0(const gchar *, priv->guids->len + 1);
		for (guint i = 0; i < priv->guids->len; i++)
			strv[i] = (const gchar *)g_ptr_array_index(priv->guids, i);
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_GUID,
				      g_variant_new_strv(strv, -1));
	}
	if (priv->flags != 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_FLAGS,
				      g_variant_new_uint64(priv->flags));
	}
	if (priv->level > 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_HSI_LEVEL,
				      g_variant_new_uint32(priv->level));
	}
	if (priv->result != FWUPD_SECURITY_ATTR_RESULT_UNKNOWN) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_HSI_RESULT,
				      g_variant_new_uint32(priv->result));
	}
	if (priv->result_fallback != FWUPD_SECURITY_ATTR_RESULT_UNKNOWN) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_HSI_RESULT_FALLBACK,
				      g_variant_new_uint32(priv->result_fallback));
	}
	if (priv->metadata != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_METADATA,
				      fwupd_hash_kv_to_variant(priv->metadata));
	}
	return g_variant_new("a{sv}", &builder);
}

/**
 * fwupd_security_attr_get_metadata:
 * @self: a #FwupdSecurityAttr
 * @key: metadata key
 *
 * Gets private metadata from the attribute which may be used in the name.
 *
 * Returns: (nullable): the metadata value, or %NULL if unfound
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_metadata(FwupdSecurityAttr *self, const gchar *key)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);

	if (priv->metadata == NULL)
		return NULL;
	return g_hash_table_lookup(priv->metadata, key);
}

/**
 * fwupd_security_attr_add_metadata:
 * @self: a #FwupdSecurityAttr
 * @key: metadata key
 * @value: (nullable): metadata value
 *
 * Adds metadata to the attribute which may be used in the name.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_add_metadata(FwupdSecurityAttr *self, const gchar *key, const gchar *value)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	g_return_if_fail(key != NULL);

	if (priv->metadata == NULL) {
		priv->metadata = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	g_hash_table_insert(priv->metadata, g_strdup(key), g_strdup(value));
}

static void
fwupd_security_attr_from_key_value(FwupdSecurityAttr *self, const gchar *key, GVariant *value)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);

	if (g_strcmp0(key, FWUPD_RESULT_KEY_APPSTREAM_ID) == 0) {
		fwupd_security_attr_set_appstream_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_CREATED) == 0) {
		fwupd_security_attr_set_created(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_NAME) == 0) {
		fwupd_security_attr_set_name(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_SUMMARY) == 0) {
		fwupd_security_attr_set_title(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DESCRIPTION) == 0) {
		fwupd_security_attr_set_description(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_PLUGIN) == 0) {
		fwupd_security_attr_set_plugin(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_URI) == 0) {
		fwupd_security_attr_set_url(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_FLAGS) == 0) {
		fwupd_security_attr_set_flags(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_HSI_LEVEL) == 0) {
		fwupd_security_attr_set_level(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_HSI_RESULT) == 0) {
		fwupd_security_attr_set_result(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_HSI_RESULT_FALLBACK) == 0) {
		fwupd_security_attr_set_result_fallback(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_GUID) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv(value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_security_attr_add_guid(self, strv[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_METADATA) == 0) {
		if (priv->metadata != NULL)
			g_hash_table_unref(priv->metadata);
		priv->metadata = fwupd_variant_to_hash_kv(value);
		return;
	}
}

static void
fwupd_pad_kv_str(GString *str, const gchar *key, const gchar *value)
{
	/* ignore */
	if (key == NULL || value == NULL)
		return;
	g_string_append_printf(str, "  %s: ", key);
	for (gsize i = strlen(key); i < 20; i++)
		g_string_append(str, " ");
	g_string_append_printf(str, "%s\n", value);
}

static void
fwupd_pad_kv_tfl(GString *str, const gchar *key, FwupdSecurityAttrFlags security_attr_flags)
{
	g_autoptr(GString) tmp = g_string_new("");
	for (guint i = 0; i < 64; i++) {
		if ((security_attr_flags & ((guint64)1 << i)) == 0)
			continue;
		g_string_append_printf(tmp,
				       "%s|",
				       fwupd_security_attr_flag_to_string((guint64)1 << i));
	}
	if (tmp->len == 0) {
		g_string_append(tmp, fwupd_security_attr_flag_to_string(0));
	} else {
		g_string_truncate(tmp, tmp->len - 1);
	}
	fwupd_pad_kv_str(str, key, tmp->str);
}

static void
fwupd_pad_kv_int(GString *str, const gchar *key, guint32 value)
{
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;
	tmp = g_strdup_printf("%" G_GUINT32_FORMAT, value);
	fwupd_pad_kv_str(str, key, tmp);
}

/**
 * fwupd_security_attr_from_json:
 * @self: a #FwupdSecurityAttr
 * @json_node: a JSON node
 * @error: (nullable): optional return location for an error
 *
 * Loads a fwupd security attribute from a JSON node.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.7.1
 **/
gboolean
fwupd_security_attr_from_json(FwupdSecurityAttr *self, JsonNode *json_node, GError **error)
{
#if JSON_CHECK_VERSION(1, 6, 0)
	JsonObject *obj;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not JSON object");
		return FALSE;
	}
	obj = json_node_get_object(json_node);

	/* this has to exist */
	if (!json_object_has_member(obj, FWUPD_RESULT_KEY_APPSTREAM_ID)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "no %s property in object",
			    FWUPD_RESULT_KEY_APPSTREAM_ID);
		return FALSE;
	}

	/* all optional */
	fwupd_security_attr_set_appstream_id(
	    self,
	    json_object_get_string_member(obj, FWUPD_RESULT_KEY_APPSTREAM_ID));
	fwupd_security_attr_set_name(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_NAME, NULL));
	fwupd_security_attr_set_title(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_SUMMARY, NULL));
	fwupd_security_attr_set_description(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_DESCRIPTION, NULL));
	fwupd_security_attr_set_plugin(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_PLUGIN, NULL));
	fwupd_security_attr_set_url(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_URI, NULL));
	fwupd_security_attr_set_level(
	    self,
	    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_HSI_LEVEL, 0));
	fwupd_security_attr_set_created(
	    self,
	    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_CREATED, 0));

	/* also optional */
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_HSI_RESULT)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_HSI_RESULT,
							       NULL);
		fwupd_security_attr_set_result(self, fwupd_security_attr_result_from_string(tmp));
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_HSI_RESULT_FALLBACK)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_HSI_RESULT_FALLBACK,
							       NULL);
		fwupd_security_attr_set_result_fallback(
		    self,
		    fwupd_security_attr_result_from_string(tmp));
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_FLAGS)) {
		JsonArray *array = json_object_get_array_member(obj, FWUPD_RESULT_KEY_FLAGS);
		for (guint i = 0; i < json_array_get_length(array); i++) {
			const gchar *tmp = json_array_get_string_element(array, i);
			FwupdSecurityAttrFlags flag = fwupd_security_attr_flag_from_string(tmp);
			if (flag != FWUPD_SECURITY_ATTR_FLAG_NONE)
				fwupd_security_attr_add_flag(self, flag);
		}
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_GUID)) {
		JsonArray *array = json_object_get_array_member(obj, FWUPD_RESULT_KEY_GUID);
		for (guint i = 0; i < json_array_get_length(array); i++) {
			const gchar *tmp = json_array_get_string_element(array, i);
			fwupd_security_attr_add_guid(self, tmp);
		}
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "json-glib version too old");
	return FALSE;
#endif
}

/**
 * fwupd_security_attr_to_json:
 * @self: a #FwupdSecurityAttr
 * @builder: a JSON builder
 *
 * Adds a fwupd security attribute to a JSON builder
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_to_json(FwupdSecurityAttr *self, JsonBuilder *builder)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(self));
	g_return_if_fail(builder != NULL);

	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->appstream_id);
	if (priv->created > 0)
		fwupd_common_json_add_int(builder, FWUPD_RESULT_KEY_CREATED, priv->created);
	fwupd_common_json_add_int(builder, FWUPD_RESULT_KEY_HSI_LEVEL, priv->level);
	fwupd_common_json_add_string(builder,
				     FWUPD_RESULT_KEY_HSI_RESULT,
				     fwupd_security_attr_result_to_string(priv->result));
	fwupd_common_json_add_string(builder,
				     FWUPD_RESULT_KEY_HSI_RESULT_FALLBACK,
				     fwupd_security_attr_result_to_string(priv->result_fallback));
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_SUMMARY, priv->title);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_PLUGIN, priv->plugin);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_URI, priv->url);
	if (priv->flags != FWUPD_SECURITY_ATTR_FLAG_NONE) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_FLAGS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->flags & ((guint64)1 << i)) == 0)
				continue;
			tmp = fwupd_security_attr_flag_to_string((guint64)1 << i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	if (priv->guids->len > 0) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_GUID);
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->guids->len; i++) {
			const gchar *guid = g_ptr_array_index(priv->guids, i);
			json_builder_add_string_value(builder, guid);
		}
		json_builder_end_array(builder);
	}
	if (priv->metadata != NULL) {
		g_autoptr(GList) keys = g_hash_table_get_keys(priv->metadata);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *key = l->data;
			const gchar *value = g_hash_table_lookup(priv->metadata, key);
			fwupd_common_json_add_string(builder, key, value);
		}
	}
}

static void
fwupd_pad_kv_unx(GString *str, const gchar *key, guint64 value)
{
	g_autoptr(GDateTime) date = NULL;
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;

	date = g_date_time_new_from_unix_utc((gint64)value);
	tmp = g_date_time_format(date, "%F");
	fwupd_pad_kv_str(str, key, tmp);
}

/**
 * fwupd_security_attr_to_string:
 * @self: a #FwupdSecurityAttr
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 1.5.0
 **/
gchar *
fwupd_security_attr_to_string(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	GString *str;

	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);

	str = g_string_new("");
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->appstream_id);
	if (priv->created > 0)
		fwupd_pad_kv_unx(str, FWUPD_RESULT_KEY_CREATED, priv->created);
	fwupd_pad_kv_int(str, FWUPD_RESULT_KEY_HSI_LEVEL, priv->level);
	fwupd_pad_kv_str(str,
			 FWUPD_RESULT_KEY_HSI_RESULT,
			 fwupd_security_attr_result_to_string(priv->result));
	fwupd_pad_kv_str(str,
			 FWUPD_RESULT_KEY_HSI_RESULT_FALLBACK,
			 fwupd_security_attr_result_to_string(priv->result_fallback));
	if (priv->flags != FWUPD_SECURITY_ATTR_FLAG_NONE)
		fwupd_pad_kv_tfl(str, FWUPD_RESULT_KEY_FLAGS, priv->flags);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_SUMMARY, priv->title);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_PLUGIN, priv->plugin);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_URI, priv->url);
	for (guint i = 0; i < priv->obsoletes->len; i++) {
		const gchar *appstream_id = g_ptr_array_index(priv->obsoletes, i);
		fwupd_pad_kv_str(str, "Obsolete", appstream_id);
	}
	for (guint i = 0; i < priv->guids->len; i++) {
		const gchar *guid = g_ptr_array_index(priv->guids, i);
		fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_GUID, guid);
	}
	if (priv->metadata != NULL) {
		g_autoptr(GList) keys = g_hash_table_get_keys(priv->metadata);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *key = l->data;
			const gchar *value = g_hash_table_lookup(priv->metadata, key);
			fwupd_pad_kv_str(str, key, value);
		}
	}

	return g_string_free(str, FALSE);
}

static void
fwupd_security_attr_class_init(FwupdSecurityAttrClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_security_attr_finalize;
}

static void
fwupd_security_attr_init(FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);
	priv->obsoletes = g_ptr_array_new_with_free_func(g_free);
	priv->guids = g_ptr_array_new_with_free_func(g_free);
	priv->created = (guint64)g_get_real_time() / G_USEC_PER_SEC;
}

static void
fwupd_security_attr_finalize(GObject *object)
{
	FwupdSecurityAttr *self = FWUPD_SECURITY_ATTR(object);
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);

	if (priv->metadata != NULL)
		g_hash_table_unref(priv->metadata);
	g_free(priv->appstream_id);
	g_free(priv->name);
	g_free(priv->title);
	g_free(priv->description);
	g_free(priv->plugin);
	g_free(priv->url);
	g_ptr_array_unref(priv->obsoletes);
	g_ptr_array_unref(priv->guids);

	G_OBJECT_CLASS(fwupd_security_attr_parent_class)->finalize(object);
}

static void
fwupd_security_attr_set_from_variant_iter(FwupdSecurityAttr *self, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		fwupd_security_attr_from_key_value(self, key, value);
		g_variant_unref(value);
	}
}

/**
 * fwupd_security_attr_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates a new security attribute using serialized data.
 *
 * Returns: (transfer full): a new #FwupdSecurityAttr, or %NULL if @value was invalid
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttr *
fwupd_security_attr_from_variant(GVariant *value)
{
	FwupdSecurityAttr *rel = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	g_return_val_if_fail(value != NULL, NULL);

	type_string = g_variant_get_type_string(value);
	if (g_strcmp0(type_string, "(a{sv})") == 0) {
		rel = fwupd_security_attr_new(NULL);
		g_variant_get(value, "(a{sv})", &iter);
		fwupd_security_attr_set_from_variant_iter(rel, iter);
	} else if (g_strcmp0(type_string, "a{sv}") == 0) {
		rel = fwupd_security_attr_new(NULL);
		g_variant_get(value, "a{sv}", &iter);
		fwupd_security_attr_set_from_variant_iter(rel, iter);
	} else {
		g_warning("type %s not known", type_string);
	}
	return rel;
}

/**
 * fwupd_security_attr_array_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates an array of new security attributes using serialized data.
 *
 * Returns: (transfer container) (element-type FwupdSecurityAttr): attributes, or %NULL if @value
 *was invalid
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_security_attr_array_from_variant(GVariant *value)
{
	GPtrArray *array = NULL;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	untuple = g_variant_get_child_value(value, 0);
	sz = g_variant_n_children(untuple);
	for (guint i = 0; i < sz; i++) {
		FwupdSecurityAttr *rel;
		g_autoptr(GVariant) data = NULL;
		data = g_variant_get_child_value(untuple, i);
		rel = fwupd_security_attr_from_variant(data);
		if (rel == NULL)
			continue;
		g_ptr_array_add(array, rel);
	}
	return array;
}

/**
 * fwupd_security_attr_copy:
 * @self: (nullable): a #FwupdSecurityAttr
 *
 * Makes a full (deep) copy of a security attribute.
 *
 * Returns: (transfer full): a new #FwupdSecurityAttr
 *
 * Since: 1.7.1
 **/
FwupdSecurityAttr *
fwupd_security_attr_copy(FwupdSecurityAttr *self)
{
	g_autoptr(FwupdSecurityAttr) new = g_object_new(FWUPD_TYPE_SECURITY_ATTR, NULL);
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(self), NULL);

	fwupd_security_attr_set_appstream_id(new, priv->appstream_id);
	fwupd_security_attr_set_name(new, priv->name);
	fwupd_security_attr_set_title(new, priv->title);
	fwupd_security_attr_set_description(new, priv->description);
	fwupd_security_attr_set_plugin(new, priv->plugin);
	fwupd_security_attr_set_url(new, priv->url);
	fwupd_security_attr_set_level(new, priv->level);
	fwupd_security_attr_set_flags(new, priv->flags);
	fwupd_security_attr_set_result(new, priv->result);
	fwupd_security_attr_set_created(new, priv->created);
	for (guint i = 0; i < priv->guids->len; i++) {
		const gchar *guid = g_ptr_array_index(priv->guids, i);
		fwupd_security_attr_add_guid(new, guid);
	}
	for (guint i = 0; i < priv->obsoletes->len; i++) {
		const gchar *obsolete = g_ptr_array_index(priv->obsoletes, i);
		fwupd_security_attr_add_obsolete(new, obsolete);
	}
	if (priv->metadata != NULL) {
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, priv->metadata);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			fwupd_security_attr_add_metadata(new,
							 (const gchar *)key,
							 (const gchar *)value);
		}
	}
	return g_steal_pointer(&new);
}

/**
 * fwupd_security_attr_new:
 * @appstream_id: (nullable): the AppStream component ID, e.g. `com.intel.BiosGuard`
 *
 * Creates a new security attribute.
 *
 * Returns: a new #FwupdSecurityAttr
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttr *
fwupd_security_attr_new(const gchar *appstream_id)
{
	FwupdSecurityAttr *self;
	self = g_object_new(FWUPD_TYPE_SECURITY_ATTR, NULL);
	if (appstream_id != NULL)
		fwupd_security_attr_set_appstream_id(self, appstream_id);
	return FWUPD_SECURITY_ATTR(self);
}
