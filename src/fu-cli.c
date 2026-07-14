/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCli"

#include "config.h"

#include <glib/gi18n.h>

#include "fu-cli-common.h"
#include "fu-cli.h"
#include "fu-console.h"

typedef struct {
	GPtrArray *cmd_array;
	FuConsole *console;
	FuCliArgFlag arg_flags;
	FwupdDeviceFlags filter_device_include;
	FwupdDeviceFlags filter_device_exclude;
	FwupdReleaseFlags filter_release_include;
	FwupdReleaseFlags filter_release_exclude;
	GPtrArray *filter_protocols_include;
	GPtrArray *filter_protocols_exclude;
} FuCliPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCli, fu_cli, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_cli_get_instance_private(o))

gboolean
fu_cli_has_arg_flag(FuCli *self, FuCliArgFlag arg_flag)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return (priv->arg_flags & arg_flag) > 0;
}

void
fu_cli_add_arg_flag(FuCli *self, FuCliArgFlag arg_flag)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	priv->arg_flags |= arg_flag;
}

FuConsole *
fu_cli_get_console(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return priv->console;
}

static gint
fu_cli_plugin_name_sort_cb(FwupdPlugin **item1, FwupdPlugin **item2)
{
	return g_strcmp0(fwupd_plugin_get_name(*item1), fwupd_plugin_get_name(*item2));
}

static gboolean
fu_cli_cmd_get_plugins(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FuCliClass *klass = FU_CLI_GET_CLASS(self);
	g_autoptr(GPtrArray) plugins = NULL;

	if (klass->get_plugins == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no impl");
		return FALSE;
	}
	plugins = klass->get_plugins(self, error);
	if (plugins == NULL)
		return FALSE;
	g_ptr_array_sort(plugins, (GCompareFunc)fu_cli_plugin_name_sort_cb);
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		fwupd_codec_array_to_json(plugins, "Plugins", json_obj, FWUPD_CODEC_FLAG_TRUSTED);
		fu_cli_print_json_object(priv->console, json_obj);
		return TRUE;
	}

	/* print */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autofree gchar *str = fu_cli_plugin_to_string(FWUPD_PLUGIN(plugin), 0);
		fu_console_print_literal(priv->console, str);
	}

	/* success */
	return TRUE;
}

typedef struct {
	FuCli *self;
	gboolean as_json;
	gboolean allow_branch_switch;
	gboolean allow_older;
	gboolean allow_reinstall;
	gboolean assume_yes;
	gboolean disable_ssl_strict;
	gboolean force;
	gboolean show_all;
	gboolean no_device_prompt;
	gboolean no_reboot_check;
	gboolean no_safety_check;
	gboolean no_unreported_check;
	gboolean no_remote_check;
	gboolean no_security_fix;
	gboolean no_metadata_check;
	gboolean sign;
	gboolean no_history;
	gboolean only_emulated;
	gchar *filter_device;
	gchar *filter_release;
	gchar **filter_protocols;
} FuCliGroupHelper;

static void
fu_cli_group_helper_free(FuCliGroupHelper *helper)
{
	g_object_unref(helper->self);
	g_free(helper->filter_device);
	g_free(helper->filter_release);
	g_strfreev(helper->filter_protocols);
	g_free(helper);
}

static gboolean
fu_cli_parse_filter_device_flags(FuCli *self, const gchar *filter, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdDeviceFlags tmp;
	g_auto(GStrv) strv = g_strsplit(filter, ",", -1);

	g_return_val_if_fail(filter != NULL, FALSE);

	for (guint i = 0; strv[i] != NULL; i++) {
		if (g_str_has_prefix(strv[i], "~")) {
			tmp = fwupd_device_flag_from_string(strv[i] + 1);
			if (tmp == FWUPD_DEVICE_FLAG_UNKNOWN) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Unknown device flag %s",
					    strv[i] + 1);
				return FALSE;
			}
			if ((tmp & priv->filter_device_include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & priv->filter_device_exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			priv->filter_device_exclude |= tmp;
		} else {
			tmp = fwupd_device_flag_from_string(strv[i]);
			if (tmp == FWUPD_DEVICE_FLAG_UNKNOWN) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Unknown device flag %s",
					    strv[i]);
				return FALSE;
			}
			if ((tmp & priv->filter_device_exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & priv->filter_device_include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			priv->filter_device_include |= tmp;
		}
	}

	return TRUE;
}

static gboolean
fu_cli_parse_filter_release_flags(FuCli *self, const gchar *filter, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdReleaseFlags tmp;
	g_auto(GStrv) strv = g_strsplit(filter, ",", -1);

	g_return_val_if_fail(filter != NULL, FALSE);

	for (guint i = 0; strv[i] != NULL; i++) {
		if (g_str_has_prefix(strv[i], "~")) {
			tmp = fwupd_release_flag_from_string(strv[i] + 1);
			if (tmp == FWUPD_RELEASE_FLAG_UNKNOWN) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Unknown release flag %s",
					    strv[i] + 1);
				return FALSE;
			}
			if ((tmp & priv->filter_release_include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_release_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & priv->filter_release_exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_release_flag_to_string(tmp));
				return FALSE;
			}
			priv->filter_release_exclude |= tmp;
		} else {
			tmp = fwupd_release_flag_from_string(strv[i]);
			if (tmp == FWUPD_RELEASE_FLAG_UNKNOWN) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Unknown release flag %s",
					    strv[i]);
				return FALSE;
			}
			if ((tmp & priv->filter_release_exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_release_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & priv->filter_release_include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_release_flag_to_string(tmp));
				return FALSE;
			}
			priv->filter_release_include |= tmp;
		}
	}

	return TRUE;
}

/* a protocol must not contain spaces and must contain at least one dot */
static gboolean
fu_cli_validate_protocol(const gchar *protocol)
{
	if (protocol == NULL)
		return FALSE;
	if (g_strstr_len(protocol, -1, ".") == NULL)
		return FALSE;
	if (g_strstr_len(protocol, -1, " ") != NULL)
		return FALSE;
	return TRUE;
}

static gboolean
fu_cli_parse_filter_protocol_flags(FuCli *self, gchar **filters, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	for (guint i = 0; filters[i] != NULL; i++) {
		const gchar *protocol = filters[i];
		if (g_str_has_prefix(protocol, "~")) {
			if (!fu_cli_validate_protocol(protocol + 1)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid filtered protocol '%s'",
					    protocol + 1);
				return FALSE;
			}
			g_ptr_array_add(priv->filter_protocols_exclude, g_strdup(protocol + 1));
		} else {
			if (!fu_cli_validate_protocol(protocol)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid filtered protocol '%s'",
					    protocol);
				return FALSE;
			}
			g_ptr_array_add(priv->filter_protocols_include, g_strdup(protocol));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_group_pre_parse_cb(GOptionContext *context,
			  GOptionGroup *group,
			  gpointer data,
			  GError **error)
{
	FuCliGroupHelper *helper = (FuCliGroupHelper *)data;
	const GOptionEntry entries[] = {
	    {"json",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->as_json,
	     /* TRANSLATORS: command line option */
	     N_("Output in JSON format (disables all interactive prompts)"),
	     NULL},
	    {"allow-reinstall",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->allow_reinstall,
	     /* TRANSLATORS: command line option */
	     N_("Allow reinstalling existing firmware versions"),
	     NULL},
	    {"allow-older",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->allow_older,
	     /* TRANSLATORS: command line option */
	     N_("Allow downgrading firmware versions"),
	     NULL},
	    {"allow-branch-switch",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->allow_branch_switch,
	     /* TRANSLATORS: command line option */
	     N_("Allow switching firmware branch"),
	     NULL},
	    {"force",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->force,
	     /* TRANSLATORS: command line option */
	     N_("Force the action by relaxing some runtime checks"),
	     NULL},
	    {"assume-yes",
	     'y',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->assume_yes,
	     /* TRANSLATORS: command line option */
	     N_("Answer yes to all questions"),
	     NULL},
	    {"sign",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->sign,
	     /* TRANSLATORS: command line option */
	     N_("Sign the uploaded data with the client certificate"),
	     NULL},
	    {"no-unreported-check",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->no_unreported_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not check for unreported history"),
	     NULL},
	    {"no-metadata-check",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->no_metadata_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not check for old metadata"),
	     NULL},
	    {"no-remote-check",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->no_remote_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not check if download remotes should be enabled"),
	     NULL},
	    {"no-reboot-check",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->no_reboot_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not check or prompt for reboot after update"),
	     NULL},
	    {"no-safety-check",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->no_safety_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not perform device safety checks"),
	     NULL},
	    {"no-device-prompt",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->no_device_prompt,
	     /* TRANSLATORS: command line option */
	     N_("Do not prompt for devices"),
	     NULL},
	    {"show-all",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->show_all,
	     /* TRANSLATORS: command line option */
	     N_("Show all results"),
	     NULL},
	    {"show-all-devices",
	     '\0',
	     G_OPTION_FLAG_HIDDEN,
	     G_OPTION_ARG_NONE,
	     &helper->show_all,
	     /* TRANSLATORS: command line option */
	     N_("Show devices that are not updatable"),
	     NULL},
	    {"disable-ssl-strict",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->disable_ssl_strict,
	     /* TRANSLATORS: command line option */
	     N_("Ignore SSL strict checks when downloading files"),
	     NULL},
	    {"no-security-fix",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->no_security_fix,
	     /* TRANSLATORS: command line option */
	     N_("Do not prompt to fix security issues"),
	     NULL},
	    {"only-emulated",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->only_emulated,
	     /* TRANSLATORS: command line option */
	     N_("Only install onto emulated devices"),
	     NULL},
	    {"no-history",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &helper->no_history,
	     /* TRANSLATORS: command line option */
	     N_("Do not write to the history database"),
	     NULL},
	    {"filter",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING,
	     &helper->filter_device,
	     /* TRANSLATORS: command line option */
	     N_("Filter with a set of device flags using a ~ prefix to "
		"exclude, e.g. 'internal,~needs-reboot'"),
	     NULL},
	    {"filter-release",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING,
	     &helper->filter_release,
	     /* TRANSLATORS: command line option */
	     N_("Filter with a set of release flags using a ~ prefix to "
		"exclude, e.g. 'trusted-release,~trusted-metadata'"),
	     NULL},
	    {"filter-protocol",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING_ARRAY,
	     &helper->filter_protocols,
	     /* TRANSLATORS: command line option */
	     N_("Filter to specific protocols, e.g. '~org.uefi.capsule' or 'org.nvmexpress'"),
	     NULL},
	    {NULL}};
	g_option_group_add_entries(group, entries);
	return TRUE;
}

static gboolean
fu_cli_group_post_parse_cb(GOptionContext *context,
			   GOptionGroup *group,
			   gpointer data,
			   GError **error)
{
	FuCliGroupHelper *helper = (FuCliGroupHelper *)data;
	FuCli *self = FU_CLI(helper->self);
	/*	FuCliPrivate *priv = GET_PRIVATE(self);*/

	/* convert to flags */
	if (helper->as_json)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON);
	if (helper->disable_ssl_strict)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_DISABLE_SSL_STRICT);
	if (helper->assume_yes)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES);
	if (helper->show_all)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_SHOW_ALL);
	if (helper->no_remote_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_REMOTE_CHECK);
	if (helper->no_metadata_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_METADATA_CHECK);
	if (helper->no_reboot_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK);
	if (helper->no_unreported_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_UNREPORTED_CHECK);
	if (helper->no_safety_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_SAFETY_CHECK);
	if (helper->no_device_prompt)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_DEVICE_PROMPT);
	if (helper->no_security_fix)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_SECURITY_FIX);
	if (helper->sign)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_SIGN);
	if (helper->only_emulated)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ONLY_EMULATED);
	if (helper->no_history)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_HISTORY);

	/* parse filter flags */
	if (helper->filter_device != NULL) {
		if (!fu_cli_parse_filter_device_flags(FU_CLI(self), helper->filter_device, error)) {
			g_autofree gchar *str =
			    /* TRANSLATORS: the user didn't read the man page, %1 is '--filter' */
			    g_strdup_printf(_("Failed to parse flags for %s"), "--filter");
			g_prefix_error(error, "%s: ", str);
			return EXIT_FAILURE;
		}
	}
	if (helper->filter_release != NULL) {
		if (!fu_cli_parse_filter_release_flags(FU_CLI(self),
						       helper->filter_release,
						       error)) {
			g_autofree gchar *str =
			    /* TRANSLATORS: the user didn't read the man page,
			     * %1 is '--filter-release' */
			    g_strdup_printf(_("Failed to parse flags for %s"), "--filter-release");
			g_prefix_error(error, "%s: ", str);
			return FALSE;
		}
	}
	if (helper->filter_protocols != NULL) {
		if (!fu_cli_parse_filter_protocol_flags(FU_CLI(self),
							helper->filter_protocols,
							error)) {
			g_autofree gchar *str =
			    /* TRANSLATORS: the user didn't read the man page,
			     * %1 is '--filter-release' */
			    g_strdup_printf(_("Failed to parse flags for %s"), "--filter-protocol");
			g_prefix_error(error, "%s: ", str);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

GOptionGroup *
fu_cli_get_option_group(FuCli *self)
{
	FuCliGroupHelper *helper = g_new0(FuCliGroupHelper, 1);
	GOptionGroup *group;

	helper->self = g_object_ref(self);
	group = g_option_group_new("cli",
				   /* TRANSLATORS: for the --verbose arg */
				   _("CLI Options"),
				   /* TRANSLATORS: for the --verbose arg */
				   _("Show CLI options"),
				   helper,
				   (GDestroyNotify)fu_cli_group_helper_free);
	g_option_group_set_parse_hooks(group,
				       fu_cli_group_pre_parse_cb,
				       fu_cli_group_post_parse_cb);
	return group;
}

void
fu_cli_cmd_array_add_common(FuCli *self)
{
	fu_cli_cmd_array_add(self,
			     "get-plugins",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Get all enabled plugins registered with the system"),
			     fu_cli_cmd_get_plugins);
}

typedef struct {
	gchar *name;
	gchar *arguments;
	gchar *description;
	FuCliCmdFlags flags;
	FuCliCmdFunc callback;
} FuCliCmd;

static void
fu_cli_cmd_free(FuCliCmd *item)
{
	g_free(item->name);
	g_free(item->arguments);
	g_free(item->description);
	g_free(item);
}

static gint
fu_cli_cmd_sort_cb(FuCliCmd **item1, FuCliCmd **item2)
{
	return g_strcmp0((*item1)->name, (*item2)->name);
}

void
fu_cli_cmd_array_sort(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_ptr_array_sort(priv->cmd_array, (GCompareFunc)fu_cli_cmd_sort_cb);
}

void
fu_cli_cmd_array_add(FuCli *self,
		     const gchar *name,
		     const gchar *arguments,
		     const gchar *description,
		     FuCliCmdFunc callback)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) names = NULL;

	g_return_if_fail(name != NULL);
	g_return_if_fail(description != NULL);
	g_return_if_fail(callback != NULL);

	/* add each one */
	names = g_strsplit(name, ",", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		FuCliCmd *item = g_new0(FuCliCmd, 1);
		item->name = g_strdup(names[i]);
		if (i == 0) {
			item->description = g_strdup(description);
		} else {
			/* TRANSLATORS: this is a command alias, e.g. 'get-devices' */
			item->description = g_strdup_printf(_("Alias to %s"), names[0]);
			item->flags |= FU_CLI_CMD_FLAG_IS_ALIAS;
		}
		item->arguments = g_strdup(arguments);
		item->callback = callback;
		g_ptr_array_add(priv->cmd_array, item);
	}
}

gboolean
fu_cli_cmd_array_run(FuCli *self, const gchar *command, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) values_copy = g_new0(gchar *, g_strv_length(values) + 1);

	/* clear out bash completion sentinel */
	for (guint i = 0; values[i] != NULL; i++) {
		if (g_strcmp0(values[i], "{") == 0) /* nocheck:depth */
			break;
		values_copy[i] = g_strdup(values[i]);
	}

	/* return all possible actions */
	if (g_strcmp0(command, "get-actions") == 0) {
		for (guint i = 0; i < priv->cmd_array->len; i++) {
			FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
			if (item->flags & FU_CLI_CMD_FLAG_IS_ALIAS)
				continue;
			g_print("%s\n", item->name); /* nocheck:print */
		}
		return TRUE;
	}

	/* find command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
		if (g_strcmp0(item->name, command) == 0)
			return item->callback(self, values_copy, error);
	}

	/* not found */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    /* TRANSLATORS: error message */
			    _("Command not found"));
	return FALSE;
}

gchar *
fu_cli_cmd_array_to_string(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	gsize len;
	const gsize max_len = 35;
	GString *string = g_string_new(NULL);

	/* print each command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
		g_string_append(string, "  ");
		g_string_append(string, item->name);
		len = fu_strwidth(item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append(string, " ");
			g_string_append(string, item->arguments);
			len += fu_strwidth(item->arguments) + 1;
		}
		if (len < max_len) {
			for (gsize j = len; j < max_len + 1; j++)
				g_string_append_c(string, ' ');
			g_string_append(string, item->description);
			g_string_append_c(string, '\n');
		} else {
			g_string_append_c(string, '\n');
			for (gsize j = 0; j < max_len + 1; j++)
				g_string_append_c(string, ' ');
			g_string_append(string, item->description);
			g_string_append_c(string, '\n');
		}
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size(string, string->len - 1);

	return g_string_free(string, FALSE);
}

void
fu_cli_print_error(FuCli *self, const GError *error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_print_error_as_json(priv->console, error);
		return;
	}
	fu_console_print_full(priv->console, FU_CONSOLE_PRINT_FLAG_STDERR, "%s\n", error->message);
}

FwupdInstallFlags
fu_cli_get_install_flags(FuCli *self)
{
	FwupdInstallFlags install_flags = 0;

	/* set flags */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_REINSTALL))
		install_flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_OLDER))
		install_flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_BRANCH_SWITCH))
		install_flags |= FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ONLY_EMULATED))
		install_flags |= FWUPD_INSTALL_FLAG_ONLY_EMULATED;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE)) {
		install_flags |= FWUPD_INSTALL_FLAG_FORCE;
		install_flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;
	}
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_HISTORY))
		install_flags |= FWUPD_INSTALL_FLAG_NO_HISTORY;

	return install_flags;
}

static gboolean
fu_cli_device_has_any_protocol(FwupdDevice *device, GPtrArray *protocols)
{
	for (guint i = 0; i < protocols->len; i++) {
		const gchar *protocol = g_ptr_array_index(protocols, i);
		if (fwupd_device_has_protocol(device, protocol))
			return TRUE;
	}
	return FALSE;
}

gboolean
fu_cli_device_match_flags(FuCli *self, FwupdDevice *device)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return fwupd_device_match_flags(device,
					priv->filter_device_include,
					priv->filter_device_exclude);
}

gboolean
fu_cli_release_match_flags(FuCli *self, FwupdRelease *release)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return fwupd_release_match_flags(release,
					 priv->filter_release_include,
					 priv->filter_release_exclude);
}

gboolean
fu_cli_device_match_protocol(FuCli *self, FwupdDevice *device)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (priv->filter_protocols_exclude->len > 0 &&
	    fu_cli_device_has_any_protocol(device, priv->filter_protocols_exclude))
		return FALSE;
	if (priv->filter_protocols_include->len > 0 &&
	    !fu_cli_device_has_any_protocol(device, priv->filter_protocols_include))
		return FALSE;

	/* success */
	return TRUE;
}

GPtrArray *
fu_cli_device_array_filter(FuCli *self, GPtrArray *devices, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) devices1 = NULL;
	g_autoptr(GPtrArray) devices2 = NULL;

	devices1 = fwupd_device_array_filter_flags(devices,
						   priv->filter_device_include,
						   priv->filter_device_exclude,
						   error);
	if (devices1 == NULL)
		return NULL;
	if (priv->filter_protocols_include->len > 0 || priv->filter_protocols_exclude->len > 0) {
		devices2 = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		for (guint i = 0; i < devices1->len; i++) {
			FwupdDevice *device = g_ptr_array_index(devices1, i);
			if (!fu_cli_device_match_protocol(self, device))
				continue;
			g_ptr_array_add(devices2, g_object_ref(device));
		}
		if (devices2->len == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "no devices");
			return NULL;
		}
	} else {
		devices2 = g_ptr_array_ref(devices1);
	}
	return g_steal_pointer(&devices2);
}
void
fu_cli_add_filter_device_include(FuCli *self, FwupdDeviceFlags device_flag)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	priv->filter_device_include |= device_flag;
}

void
fu_cli_add_filter_device_exclude(FuCli *self, FwupdDeviceFlags device_flag)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	priv->filter_device_exclude |= device_flag;
}

GPtrArray *
fu_cli_release_array_filter_flags(FuCli *self, GPtrArray *rels, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return fwupd_release_array_filter_flags(rels,
						priv->filter_release_include,
						priv->filter_release_exclude,
						error);
}

static void
fu_cli_init(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	priv->cmd_array = g_ptr_array_new_with_free_func((GDestroyNotify)fu_cli_cmd_free);
	priv->console = fu_console_new();
	priv->filter_protocols_include = g_ptr_array_new_with_free_func(g_free);
	priv->filter_protocols_exclude = g_ptr_array_new_with_free_func(g_free);
}

static void
fu_cli_finalize(GObject *obj)
{
	FuCli *self = FU_CLI(obj);
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_ptr_array_unref(priv->filter_protocols_include);
	g_ptr_array_unref(priv->filter_protocols_exclude);
	g_object_unref(priv->console);
	g_ptr_array_unref(priv->cmd_array);
	G_OBJECT_CLASS(fu_cli_parent_class)->finalize(obj);
}

static void
fu_cli_class_init(FuCliClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cli_finalize;
}

FuCli *
fu_cli_new(void)
{
	return g_object_new(FU_TYPE_CLI, NULL);
}
