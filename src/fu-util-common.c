/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuMain"

#include <config.h>

#include <stdio.h>
#include <glib/gi18n.h>
#include <gusb.h>
#include <xmlb.h>

#include "fu-common.h"
#include "fu-util-common.h"
#include "fu-device.h"

#ifdef HAVE_SYSTEMD
#include "fu-systemd.h"
#endif

#define SYSTEMD_FWUPD_UNIT		"fwupd.service"
#define SYSTEMD_SNAP_FWUPD_UNIT		"snap.fwupd.fwupd.service"

const gchar *
fu_util_get_systemd_unit (void)
{
	if (g_getenv ("SNAP") != NULL)
		return SYSTEMD_SNAP_FWUPD_UNIT;
	return SYSTEMD_FWUPD_UNIT;
}

#ifdef HAVE_SYSTEMD
static const gchar *
fu_util_get_expected_command (const gchar *target)
{
	if (g_strcmp0 (target, SYSTEMD_SNAP_FWUPD_UNIT) == 0)
		return "fwupd.fwupdmgr";
	return "fwupdmgr";
}
#endif

gboolean
fu_util_using_correct_daemon (GError **error)
{
#ifdef HAVE_SYSTEMD
	g_autofree gchar *default_target = NULL;
	g_autoptr(GError) error_local = NULL;
	const gchar *target = fu_util_get_systemd_unit ();

	default_target = fu_systemd_get_default_target (&error_local);
	if (default_target == NULL) {
		g_debug ("Systemd isn't accessible: %s\n", error_local->message);
		return TRUE;
	}
	if (!fu_systemd_unit_check_exists (target, &error_local)) {
		g_debug ("wrong target: %s\n", error_local->message);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_ARGS,
			     /* TRANSLATORS: error message */
			     _("Mismatched daemon and client, use %s instead"),
			     fu_util_get_expected_command (target));
		return FALSE;
	}
#endif
	return TRUE;
}

void
fu_util_print_data (const gchar *title, const gchar *msg)
{
	gsize title_len;
	g_auto(GStrv) lines = NULL;

	if (msg == NULL)
		return;
	g_print ("%s:", title);

	/* pad */
	title_len = strlen (title) + 1;
	lines = g_strsplit (msg, "\n", -1);
	for (guint j = 0; lines[j] != NULL; j++) {
		for (gsize i = title_len; i < 25; i++)
			g_print (" ");
		g_print ("%s\n", lines[j]);
		title_len = 0;
	}
}

guint
fu_util_prompt_for_number (guint maxnum)
{
	gint retval;
	guint answer = 0;

	do {
		char buffer[64];

		/* swallow the \n at end of line too */
		if (!fgets (buffer, sizeof (buffer), stdin))
			break;
		if (strlen (buffer) == sizeof (buffer) - 1)
			continue;

		/* get a number */
		retval = sscanf (buffer, "%u", &answer);

		/* positive */
		if (retval == 1 && answer <= maxnum)
			break;

		/* TRANSLATORS: the user isn't reading the question */
		g_print (_("Please enter a number from 0 to %u: "), maxnum);
	} while (TRUE);
	return answer;
}

gboolean
fu_util_prompt_for_boolean (gboolean def)
{
	do {
		char buffer[4];
		if (!fgets (buffer, sizeof (buffer), stdin))
			continue;
		if (strlen (buffer) == sizeof (buffer) - 1)
			continue;
		if (g_strcmp0 (buffer, "\n") == 0)
			return def;
		buffer[0] = g_ascii_toupper (buffer[0]);
		if (g_strcmp0 (buffer, "Y\n") == 0)
			return TRUE;
		if (g_strcmp0 (buffer, "N\n") == 0)
			return FALSE;
	} while (TRUE);
	return FALSE;
}

static gboolean
fu_util_traverse_tree (GNode *n, gpointer data)
{
	guint idx = g_node_depth (n) - 1;
	g_autofree gchar *tmp = NULL;
	g_auto(GStrv) split = NULL;

	/* get split lines */
	if (FWUPD_IS_DEVICE (n->data)) {
		FwupdDevice *dev = FWUPD_DEVICE (n->data);
		tmp = fu_util_device_to_string (dev, idx);
	} else if (FWUPD_IS_REMOTE (n->data)) {
		FwupdRemote *remote = FWUPD_REMOTE (n->data);
		tmp = fu_util_remote_to_string (remote, idx);
	} else if (FWUPD_IS_RELEASE (n->data)) {
		FwupdRelease *release = FWUPD_RELEASE (n->data);
		tmp = fu_util_release_to_string (release, idx);
		g_debug ("%s", tmp);
	}

	/* root node */
	if (n->data == NULL && g_getenv ("FWUPD_VERBOSE") == NULL) {
		g_print ("○\n");
		return FALSE;
	}
	if (n->parent == NULL)
		return FALSE;

	if (tmp == NULL)
		return FALSE;
	split = g_strsplit (tmp, "\n", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		g_autoptr(GString) str = g_string_new (NULL);

		/* header */
		if (i == 0) {
			if (g_node_next_sibling (n) == NULL)
				g_string_prepend (str, "└─");
			else
				g_string_prepend (str, "├─");

		/* properties */
		} else {
			g_string_prepend (str, n->children == NULL ? "  " : " │");
			g_string_prepend (str, g_node_next_sibling (n) == NULL ? " " : "│");
			g_string_append (str, " ");
		}

		/* ancestors */
		for (GNode *c = n->parent; c->parent != NULL; c = c->parent) {
			if (g_node_next_sibling (c) != NULL || idx == 0) {
				g_string_prepend (str, "│ ");
				continue;
			}
			g_string_prepend (str, "  ");
		}

		/* empty line */
		if (split[i][0] == '\0') {
			g_print ("%s\n", str->str);
			continue;
		}

		/* dump to the console */
		g_string_append (str, split[i] + (idx * 2));
		g_print ("%s\n", str->str);
	}

	return FALSE;
}

void
fu_util_print_tree (GNode *n, gpointer data)
{
	g_node_traverse (n, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
			 fu_util_traverse_tree, data);
}

gboolean
fu_util_is_interesting_device (FwupdDevice *dev)
{
	if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE))
		return TRUE;
	if (fwupd_device_get_update_error (dev) != NULL)
		return TRUE;
	return FALSE;
}

gchar *
fu_util_get_user_cache_path (const gchar *fn)
{
	const gchar *root = g_get_user_cache_dir ();
	g_autofree gchar *basename = g_path_get_basename (fn);
	g_autofree gchar *cachedir_legacy = NULL;

	/* if run from a systemd unit, use the cache directory set there */
	if (g_getenv ("CACHE_DIRECTORY") != NULL)
		root = g_getenv ("CACHE_DIRECTORY");

	/* return the legacy path if it exists rather than renaming it to
	 * prevent problems when using old and new versions of fwupd */
	cachedir_legacy = g_build_filename (root, "fwupdmgr", NULL);
	if (g_file_test (cachedir_legacy, G_FILE_TEST_IS_DIR))
		return g_build_filename (cachedir_legacy, basename, NULL);

	return g_build_filename (root, "fwupd", basename, NULL);
}

gchar *
fu_util_get_versions (void)
{
	GString *string = g_string_new ("");

	g_string_append_printf (string,
				"client version:\t%i.%i.%i\n",
				FWUPD_MAJOR_VERSION,
				FWUPD_MINOR_VERSION,
				FWUPD_MICRO_VERSION);
#ifdef FWUPD_GIT_DESCRIBE
	g_string_append_printf (string,
				"checkout info:\t%s\n", FWUPD_GIT_DESCRIBE);
#endif
	g_string_append_printf (string,
				"compile-time dependency versions\n");
	g_string_append_printf (string,
				"\tgusb:\t%d.%d.%d\n",
				G_USB_MAJOR_VERSION,
				G_USB_MINOR_VERSION,
				G_USB_MICRO_VERSION);
#ifdef EFIVAR_LIBRARY_VERSION
	g_string_append_printf (string,
				"\tefivar:\t%s",
				EFIVAR_LIBRARY_VERSION);
#endif
	return g_string_free (string, FALSE);
}

static gboolean
fu_util_update_shutdown (GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;

#ifdef HAVE_LOGIND
	/* shutdown using logind */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.login1",
					   "/org/freedesktop/login1",
					   "org.freedesktop.login1.Manager",
					   "PowerOff",
					   g_variant_new ("(b)", TRUE),
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   error);
#elif defined(HAVE_CONSOLEKIT)
	/* shutdown using ConsoleKit */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.ConsoleKit",
					   "/org/freedesktop/ConsoleKit/Manager",
					   "org.freedesktop.ConsoleKit.Manager",
					   "Stop",
					   NULL,
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_ARGS,
			     "No supported backend compiled in to perform the operation.");
#endif
	return val != NULL;
}

gboolean
fu_util_update_reboot (GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;

#ifdef HAVE_LOGIND
	/* reboot using logind */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.login1",
					   "/org/freedesktop/login1",
					   "org.freedesktop.login1.Manager",
					   "Reboot",
					   g_variant_new ("(b)", TRUE),
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   error);
#elif defined(HAVE_CONSOLEKIT)
	/* reboot using ConsoleKit */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.ConsoleKit",
					   "/org/freedesktop/ConsoleKit/Manager",
					   "org.freedesktop.ConsoleKit.Manager",
					   "Restart",
					   NULL,
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_ARGS,
			     "No supported backend compiled in to perform the operation.");
#endif
	return val != NULL;
}

gboolean
fu_util_prompt_complete (FwupdDeviceFlags flags, gboolean prompt, GError **error)
{
	if (flags & FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN) {
		if (prompt) {
			g_print ("\n%s %s [Y|n]: ",
				 /* TRANSLATORS: explain why we want to shutdown */
				 _("An update requires the system to shutdown to complete."),
				 /* TRANSLATORS: shutdown to apply the update */
				 _("Shutdown now?"));
			if (!fu_util_prompt_for_boolean (TRUE))
				return TRUE;
		}
		return fu_util_update_shutdown (error);
	}
	if (flags & FWUPD_DEVICE_FLAG_NEEDS_REBOOT) {
		if (prompt) {
			g_print ("\n%s %s [Y|n]: ",
				 /* TRANSLATORS: explain why we want to reboot */
				 _("An update requires a reboot to complete."),
				 /* TRANSLATORS: reboot to apply the update */
				 _("Restart now?"));
			if (!fu_util_prompt_for_boolean (TRUE))
				return TRUE;
		}
		return fu_util_update_reboot (error);
	}

	return TRUE;
}

static void
fu_util_cmd_free (FuUtilCmd *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

GPtrArray *
fu_util_cmd_array_new (void)
{
	return g_ptr_array_new_with_free_func ((GDestroyNotify) fu_util_cmd_free);
}

static gint
fu_util_cmd_sort_cb (FuUtilCmd **item1, FuUtilCmd **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

void
fu_util_cmd_array_sort (GPtrArray *array)
{
	g_ptr_array_sort (array, (GCompareFunc) fu_util_cmd_sort_cb);
}

void
fu_util_cmd_array_add (GPtrArray *array,
		       const gchar *name,
		       const gchar *arguments,
		       const gchar *description,
		       FuUtilCmdFunc callback)
{
	g_auto(GStrv) names = NULL;

	g_return_if_fail (name != NULL);
	g_return_if_fail (description != NULL);
	g_return_if_fail (callback != NULL);

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		FuUtilCmd *item = g_new0 (FuUtilCmd, 1);
		item->name = g_strdup (names[i]);
		if (i == 0) {
			item->description = g_strdup (description);
		} else {
			/* TRANSLATORS: this is a command alias, e.g. 'get-devices' */
			item->description = g_strdup_printf (_("Alias to %s"),
							     names[0]);
		}
		item->arguments = g_strdup (arguments);
		item->callback = callback;
		g_ptr_array_add (array, item);
	}
}

gboolean
fu_util_cmd_array_run (GPtrArray *array,
		       FuUtilPrivate *priv,
		       const gchar *command,
		       gchar **values,
		       GError **error)
{
	/* find command */
	for (guint i = 0; i < array->len; i++) {
		FuUtilCmd *item = g_ptr_array_index (array, i);
		if (g_strcmp0 (item->name, command) == 0)
			return item->callback (priv, values, error);
	}

	/* not found */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_ARGS,
			     /* TRANSLATORS: error message */
			     _("Command not found"));
	return FALSE;
}

gchar *
fu_util_cmd_array_to_string (GPtrArray *array)
{
	gsize len;
	const gsize max_len = 35;
	GString *string;

	/* print each command */
	string = g_string_new ("");
	for (guint i = 0; i < array->len; i++) {
		FuUtilCmd *item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		len = strlen (item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append (string, " ");
			g_string_append (string, item->arguments);
			len += strlen (item->arguments) + 1;
		}
		if (len < max_len) {
			for (gsize j = len; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		} else {
			g_string_append_c (string, '\n');
			for (gsize j = 0; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		}
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

SoupSession *
fu_util_setup_networking (GError **error)
{
	const gchar *http_proxy;
	g_autofree gchar *user_agent = NULL;
	g_autoptr(SoupSession) session = NULL;

	/* create the soup session */
	user_agent = fwupd_build_user_agent (PACKAGE_NAME, PACKAGE_VERSION);
	session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, user_agent,
						 SOUP_SESSION_TIMEOUT, 60,
						 NULL);
	if (session == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to setup networking");
		return NULL;
	}

	/* set the proxy */
	http_proxy = g_getenv ("https_proxy");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("HTTPS_PROXY");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("http_proxy");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("HTTP_PROXY");
	if (http_proxy != NULL && strlen (http_proxy) > 0) {
		g_autoptr(SoupURI) proxy_uri = soup_uri_new (http_proxy);
		if (proxy_uri == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid proxy URI: %s", http_proxy);
			return NULL;
		}
		g_object_set (session, SOUP_SESSION_PROXY_URI, proxy_uri, NULL);
	}

	/* this disables the double-compression of the firmware.xml.gz file */
	soup_session_remove_feature_by_type (session, SOUP_TYPE_CONTENT_DECODER);
	return g_steal_pointer (&session);
}

gchar *
fu_util_release_get_name (FwupdRelease *release)
{
	const gchar *name = fwupd_release_get_name (release);
	GPtrArray *cats = fwupd_release_get_categories (release);

	for (guint i = 0; i < cats->len; i++) {
		const gchar *cat = g_ptr_array_index (cats, i);
		if (g_strcmp0 (cat, "X-Device") == 0) {
			/* TRANSLATORS: a specific part of hardware,
			 * the first %s is the device name, e.g. 'Unifying Receiver` */
			return g_strdup_printf (_("%s Device Update"), name);
		}
		if (g_strcmp0 (cat, "X-System") == 0) {
			/* TRANSLATORS: the entire system, e.g. all internal devices,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s System Update"), name);
		}
		if (g_strcmp0 (cat, "X-EmbeddedController") == 0) {
			/* TRANSLATORS: the EC is typically the keyboard controller chip,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s Embedded Controller Update"), name);
		}
		if (g_strcmp0 (cat, "X-ManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine, the Intel AMT thing,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s ME Update"), name);
		}
		if (g_strcmp0 (cat, "X-CorporateManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine (with Intel AMT),
			 * where the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s Corporate ME Update"), name);
		}
		if (g_strcmp0 (cat, "X-ConsumerManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine, where
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s Consumer ME Update"), name);
		}
		if (g_strcmp0 (cat, "X-Controller") == 0) {
			/* TRANSLATORS: the controller is a device that has other devices
			 * plugged into it, for example ThunderBolt, FireWire or USB,
			 * the first %s is the device name, e.g. 'Intel ThunderBolt` */
			return g_strdup_printf (_("%s Controller Update"), name);
		}
		if (g_strcmp0 (cat, "X-ThunderboltController") == 0) {
			/* TRANSLATORS: the Thunderbolt controller is a device that
			 * has other high speed Thunderbolt devices plugged into it;
			 * the first %s is the system name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s Thunderbolt Controller Update"), name);
		}
	}

	/* TRANSLATORS: this is the fallback where we don't know if the release
	 * is updating the system, the device, or a device class, or something else --
	 * the first %s is the device name, e.g. 'ThinkPad P50` */
	return g_strdup_printf (_("%s Update"), name);
}

static GPtrArray *
fu_util_strsplit_words (const gchar *text, guint line_len)
{
	g_auto(GStrv) tokens = NULL;
	g_autoptr(GPtrArray) lines = g_ptr_array_new ();
	g_autoptr(GString) curline = g_string_new (NULL);

	/* sanity check */
	if (text == NULL || text[0] == '\0')
		return NULL;
	if (line_len == 0)
		return NULL;

	/* tokenize the string */
	tokens = g_strsplit (text, " ", -1);
	for (guint i = 0; tokens[i] != NULL; i++) {

		/* current line plus new token is okay */
		if (curline->len + strlen (tokens[i]) < line_len) {
			g_string_append_printf (curline, "%s ", tokens[i]);
			continue;
		}

		/* too long, so remove space, add newline and dump */
		if (curline->len > 0)
			g_string_truncate (curline, curline->len - 1);
		g_ptr_array_add (lines, g_strdup (curline->str));
		g_string_truncate (curline, 0);
		g_string_append_printf (curline, "%s ", tokens[i]);
	}

	/* any incomplete line? */
	if (curline->len > 0) {
		g_string_truncate (curline, curline->len - 1);
		g_ptr_array_add (lines, g_strdup (curline->str));
	}
	return g_steal_pointer (&lines);
}

static void
fu_util_warning_box_line (const gchar *start,
			  const gchar *text,
			  const gchar *end,
			  const gchar *padding,
			  guint width)
{
	guint offset = 0;
	if (start != NULL) {
		offset += g_utf8_strlen (start, -1);
		g_print ("%s", start);
	}
	if (text != NULL) {
		offset += g_utf8_strlen (text, -1);
		g_print ("%s", text);
	}
	if (end != NULL)
		offset += g_utf8_strlen (end, -1);
	for (guint i = offset; i < width; i++)
		g_print ("%s", padding);
	if (end != NULL)
		g_print ("%s\n", end);
}

void
fu_util_warning_box (const gchar *str, guint width)
{
	g_auto(GStrv) split = g_strsplit (str, "\n", -1);

	/* header */
	fu_util_warning_box_line ("╔", NULL, "╗", "═", width);

	/* body */
	for (guint i = 0; split[i] != NULL; i++) {
		g_autoptr(GPtrArray) lines = fu_util_strsplit_words (split[i], width - 4);
		if (lines == NULL)
			continue;
		for (guint j = 0; j < lines->len; j++) {
			const gchar *line = g_ptr_array_index (lines, j);
			fu_util_warning_box_line ("║ ", line, " ║", " ", width);
		}
		fu_util_warning_box_line ("║", NULL, "║", " ", width);
	}

	/* footer */
	fu_util_warning_box_line ("╚", NULL, "╝", "═", width);
}

gboolean
fu_util_parse_filter_flags (const gchar *filter, FwupdDeviceFlags *include,
			    FwupdDeviceFlags *exclude, GError **error)
{
	FwupdDeviceFlags tmp;
	g_auto(GStrv) strv = g_strsplit (filter, ",", -1);

	g_return_val_if_fail (include != NULL, FALSE);
	g_return_val_if_fail (exclude != NULL, FALSE);

	for (guint i = 0; strv[i] != NULL; i++) {
		if (g_str_has_prefix (strv[i], "~")) {
			tmp = fwupd_device_flag_from_string (strv[i] + 1);
			if (tmp == FWUPD_DEVICE_FLAG_UNKNOWN) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Unknown device flag %s",
					     strv[i] + 1);
				return FALSE;
			}
			if ((tmp & *include) > 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Filter %s already included",
					     fwupd_device_flag_to_string (tmp));
				return FALSE;
			}
			if ((tmp & *exclude) > 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Filter %s already excluded",
					     fwupd_device_flag_to_string (tmp));
				return FALSE;
			}
			*exclude |= tmp;
		} else {
			tmp = fwupd_device_flag_from_string (strv[i]);
			if (tmp == FWUPD_DEVICE_FLAG_UNKNOWN) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Unknown device flag %s",
					     strv[i]);
				return FALSE;
			}
			if ((tmp & *exclude) > 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Filter %s already excluded",
					     fwupd_device_flag_to_string (tmp));
				return FALSE;
			}
			if ((tmp & *include) > 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Filter %s already included",
					     fwupd_device_flag_to_string (tmp));
				return FALSE;
			}
			*include |= tmp;
		}
	}

	return TRUE;
}

gchar *
fu_util_convert_description (const gchar *xml, GError **error)
{
	g_autoptr(GString) str = g_string_new (NULL);
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* parse XML */
	silo = xb_silo_new_from_xml (xml, error);
	if (silo == NULL)
		return NULL;

	n = xb_silo_get_root (silo);
	while (n != NULL) {
		g_autoptr(XbNode) n2 = NULL;

		/* support <p>, <ul>, <ol> and <li>, ignore all else */
		if (g_strcmp0 (xb_node_get_element (n), "p") == 0) {
			g_string_append_printf (str, "%s\n\n", xb_node_get_text (n));
		} else if (g_strcmp0 (xb_node_get_element (n), "ul") == 0) {
			g_autoptr(GPtrArray) children = xb_node_get_children (n);
			for (guint i = 0; i < children->len; i++) {
				XbNode *nc = g_ptr_array_index (children, i);
				if (g_strcmp0 (xb_node_get_element (nc), "li") == 0) {
					g_string_append_printf (str, " • %s\n",
								xb_node_get_text (nc));
				}
			}
			g_string_append (str, "\n");
		} else if (g_strcmp0 (xb_node_get_element (n), "ol") == 0) {
			g_autoptr(GPtrArray) children = xb_node_get_children (n);
			for (guint i = 0; i < children->len; i++) {
				XbNode *nc = g_ptr_array_index (children, i);
				if (g_strcmp0 (xb_node_get_element (nc), "li") == 0) {
					g_string_append_printf (str, " %u. %s\n",
								i + 1,
								xb_node_get_text (nc));
				}
			}
			g_string_append (str, "\n");
		}

		n2 = xb_node_get_next (n);
		g_set_object (&n, n2);
	}

	/* remove extra newline */
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);

	/* success */
	return g_string_free (g_steal_pointer (&str), FALSE);
}

static gchar *
fu_util_time_to_str (guint64 tmp)
{
	g_return_val_if_fail (tmp != 0, NULL);

	/* seconds */
	if (tmp < 60) {
		/* TRANSLATORS: duration in seconds */
		return g_strdup_printf (ngettext ("%u second", "%u seconds",
						  (gint) tmp),
					(guint) tmp);
	}

	/* minutes */
	tmp /= 60;
	if (tmp < 60) {
		/* TRANSLATORS: duration in minutes */
		return g_strdup_printf (ngettext ("%u minute", "%u minutes",
						  (gint) tmp),
					(guint) tmp);
	}

	/* hours */
	tmp /= 60;
	if (tmp < 60) {
		/* TRANSLATORS: duration in minutes */
		return g_strdup_printf (ngettext ("%u hour", "%u hours",
						  (gint) tmp),
					(guint) tmp);
	}

	/* days */
	tmp /= 24;
	/* TRANSLATORS: duration in days! */
	return g_strdup_printf (ngettext ("%u day", "%u days",
					  (gint) tmp),
				(guint) tmp);
}

gchar *
fu_util_device_to_string (FwupdDevice *dev, guint idt)
{
	FwupdUpdateState state;
	GString *str = g_string_new (NULL);
	const gchar *tmp;
	const gchar *tmp2;
	guint64 flags = fwupd_device_get_flags (dev);
	g_autoptr(GString) flags_str = g_string_new (NULL);

	/* some fields are intentionally not included and are only shown in --verbose */
	if (g_getenv ("FWUPD_VERBOSE") != NULL) {
		g_autofree gchar *debug_str = NULL;
		debug_str = fwupd_device_to_string (dev);
		g_debug ("%s", debug_str);
		return NULL;
	}

	tmp = fwupd_device_get_name (dev);
	if (tmp == NULL) {
		/* TRANSLATORS: Name of hardware */
		tmp = _("Unknown Device");
	}
	fu_common_string_append_kv (str, idt, tmp, NULL);

	tmp = fwupd_device_get_id (dev);
	if (tmp != NULL) {
		/* TRANSLATORS: ID for hardware, typically a SHA1 sum */
		fu_common_string_append_kv (str, idt + 1, _("Device ID"), tmp);
	}

	/* summary */
	tmp = fwupd_device_get_summary (dev);
	if (tmp != NULL) {
		/* TRANSLATORS: one line summary of device */
		fu_common_string_append_kv (str, idt + 1, _("Summary"), tmp);
	}

	/* description */
	tmp = fwupd_device_get_description (dev);
	if (tmp != NULL) {
		g_autofree gchar *desc = NULL;
		desc = fu_util_convert_description (tmp, NULL);
		/* TRANSLATORS: multiline description of device */
		fu_common_string_append_kv (str, idt + 1, _("Description"), desc);
	}

	/* versions */
	tmp = fwupd_device_get_version (dev);
	if (tmp != NULL) {
		/* TRANSLATORS: version number of current firmware */
		fu_common_string_append_kv (str, idt + 1, _("Current version"), tmp);
	}
	tmp = fwupd_device_get_version_lowest (dev);
	if (tmp != NULL) {
		/* TRANSLATORS: smallest version number installable on device */
		fu_common_string_append_kv (str, idt + 1, _("Minimum Version"), tmp);
	}
	tmp = fwupd_device_get_version_bootloader (dev);
	if (tmp != NULL) {
		/* TRANSLATORS: firmware version of bootloader */
		fu_common_string_append_kv (str, idt + 1, _("Bootloader Version"), tmp);
	}

	/* vendor */
	tmp = fwupd_device_get_vendor (dev);
	tmp2 = fwupd_device_get_vendor_id (dev);
	if (tmp != NULL && tmp2 != NULL) {
		g_autofree gchar *both = g_strdup_printf ("%s (%s)", tmp, tmp2);
		/* TRANSLATORS: manufacturer of hardware */
		fu_common_string_append_kv (str, idt + 1, _("Vendor"), both);
	} else if (tmp != NULL) {
		/* TRANSLATORS: manufacturer of hardware */
		fu_common_string_append_kv (str, idt + 1, _("Vendor"), tmp);
	} else if (tmp2 != NULL) {
		/* TRANSLATORS: manufacturer of hardware */
		fu_common_string_append_kv (str, idt + 1, _("Vendor"), tmp2);
	}

	/* install duration */
	if (fwupd_device_get_install_duration (dev) > 0) {
		g_autofree gchar *time = fu_util_time_to_str (fwupd_device_get_install_duration (dev));
		/* TRANSLATORS: length of time the update takes to apply */
		fu_common_string_append_kv (str, idt + 1, _("Install Duration"), time);
	}

	/* serial # */
	tmp = fwupd_device_get_serial (dev);
	if (tmp != NULL) {
		/* TRANSLATORS: serial number of hardware */
		fu_common_string_append_kv (str, idt + 1, _("Serial Number"), tmp);
	}

	/* update state */
	state = fwupd_device_get_update_state (dev);
	if (state != FWUPD_UPDATE_STATE_UNKNOWN) {
		/* TRANSLATORS: hardware state, e.g. "pending" */
		fu_common_string_append_kv (str, idt + 1, _("Update State"),
					    fwupd_update_state_to_string (state));
	}
	tmp = fwupd_device_get_update_error (dev);
	if (tmp != NULL) {
		/* TRANSLATORS: error message from last update attempt */
		fu_common_string_append_kv (str, idt + 1, _("Update Error"), tmp);
	}
	tmp = fwupd_device_get_update_message (dev);
	if (tmp != NULL) {
		/* TRANSLATORS: helpful messages from last update */
		fu_common_string_append_kv (str, idt + 1, _("Update Message"), tmp);
	}

	for (guint i = 0; i < 64; i++) {
		if ((flags & ((guint64) 1 << i)) == 0)
			continue;
		g_string_append_printf (flags_str, "%s|",
					fwupd_device_flag_to_string ((guint64) 1 << i));
	}
	if (flags_str->len > 0) {
		g_string_truncate (flags_str, flags_str->len - 1);
		/* TRANSLATORS: device properties */
		fu_common_string_append_kv (str, idt + 1, _("Flags"), flags_str->str);
	}

	return g_string_free (str, FALSE);
}

gchar *
fu_util_release_to_string (FwupdRelease *rel, guint idt)
{
	GString *str = g_string_new (NULL);
	guint64 flags = fwupd_release_get_flags (rel);
	g_autoptr(GString) flags_str = g_string_new (NULL);

	g_return_val_if_fail (FWUPD_IS_RELEASE (rel), NULL);

	fu_common_string_append_kv (str, idt, fwupd_release_get_name (rel), NULL);

	/* TRANSLATORS: version number of new firmware */
	fu_common_string_append_kv (str, idt + 1 , _("Version"),
				    fwupd_release_get_version (rel));

	if (fwupd_release_get_remote_id (rel) != NULL) {
		/* TRANSLATORS: the server the file is coming from */
		fu_common_string_append_kv (str, idt + 1, _("Remote ID"),
					    fwupd_release_get_remote_id (rel));
	}
	if (fwupd_release_get_summary (rel) != NULL) {
		/* TRANSLATORS: one line summary of device */
		fu_common_string_append_kv (str, idt + 1, _("Summary"),
					    fwupd_release_get_summary (rel));
	}
	if (fwupd_release_get_license (rel) != NULL) {
		/* TRANSLATORS: e.g. GPLv2+, Non free etc */
		fu_common_string_append_kv (str, idt + 1, _("License"),
					    fwupd_release_get_license (rel));
	}
	if (fwupd_release_get_size (rel) != 0) {
		g_autofree gchar *tmp = NULL;
		tmp = g_format_size (fwupd_release_get_size (rel));
		/* TRANSLATORS: file size of the download */
		fu_common_string_append_kv (str, idt + 1, _("Size"), tmp);
	}
	if (fwupd_release_get_details_url (rel) != NULL) {
		/* TRANSLATORS: more details about the update link */
		fu_common_string_append_kv (str, idt + 1, _("Details"),
					    fwupd_release_get_details_url (rel));
	}
	if (fwupd_release_get_source_url (rel) != NULL) {
		/* TRANSLATORS: source (as in code) link */
		fu_common_string_append_kv (str, idt + 1, _("Source"),
					    fwupd_release_get_source_url (rel));
	}
	if (fwupd_release_get_vendor (rel) != NULL) {
		/* TRANSLATORS: manufacturer of hardware */
		fu_common_string_append_kv (str, idt + 1, _("Vendor"),
					    fwupd_release_get_vendor (rel));
	}
	if (fwupd_release_get_install_duration (rel) != 0) {
		g_autofree gchar *tmp = fu_util_time_to_str (fwupd_release_get_install_duration (rel));
		/* TRANSLATORS: length of time the update takes to apply */
		fu_common_string_append_kv (str, idt + 1, _("Duration"), tmp);
	}
	if (fwupd_release_get_update_message (rel) != NULL) {
		/* TRANSLATORS: helpful messages for the update */
		fu_common_string_append_kv (str, idt + 1, _("Update Message"),
					    fwupd_release_get_update_message (rel));
	}

	for (guint i = 0; i < 64; i++) {
		if ((flags & ((guint64) 1 << i)) == 0)
			continue;
		g_string_append_printf (flags_str, "%s|",
					fwupd_release_flag_to_string ((guint64) 1 << i));
	}
	if (flags_str->len == 0) {
		/* TRANSLATORS: release properties */
		fu_common_string_append_kv (str, idt + 1, _("Flags"), fwupd_release_flag_to_string (0));
	} else {
		g_string_truncate (flags_str, flags_str->len - 1);
		/* TRANSLATORS: release properties */
		fu_common_string_append_kv (str, idt + 1, _("Flags"), flags_str->str);
	}
	if (fwupd_release_get_description (rel) != NULL) {
		g_autofree gchar *desc = NULL;
		desc = fu_util_convert_description (fwupd_release_get_description (rel), NULL);
		/* TRANSLATORS: multiline description of device */
		fu_common_string_append_kv (str, idt + 1, _("Description"), desc);
	}

	return g_string_free (str, FALSE);
}

gchar *
fu_util_remote_to_string (FwupdRemote *remote, guint idt)
{
	GString *str = g_string_new (NULL);
	FwupdRemoteKind kind = fwupd_remote_get_kind (remote);
	FwupdKeyringKind keyring_kind = fwupd_remote_get_keyring_kind (remote);
	const gchar *tmp;
	gint priority;
	gdouble age;

	g_return_val_if_fail (FWUPD_IS_REMOTE (remote), NULL);

	fu_common_string_append_kv (str, idt,
				    fwupd_remote_get_title (remote), NULL);

	/* TRANSLATORS: remote identifier, e.g. lvfs-testing */
	fu_common_string_append_kv (str, idt + 1, _("Remote ID"),
				    fwupd_remote_get_id (remote));

	/* TRANSLATORS: remote type, e.g. remote or local */
	fu_common_string_append_kv (str, idt + 1, _("Type"),
				    fwupd_remote_kind_to_string (kind));

	/* TRANSLATORS: keyring type, e.g. GPG or PKCS7 */
	if (keyring_kind != FWUPD_KEYRING_KIND_UNKNOWN) {
		fu_common_string_append_kv (str, idt + 1, _("Keyring"),
					    fwupd_keyring_kind_to_string (keyring_kind));
	}

	/* TRANSLATORS: if the remote is enabled */
	fu_common_string_append_kv (str, idt + 1, _("Enabled"),
				    fwupd_remote_get_enabled (remote) ? "true" : "false");

	tmp = fwupd_remote_get_checksum (remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote checksum */
		fu_common_string_append_kv (str, idt + 1, _("Checksum"), tmp);
	}

	/* optional parameters */
	age = fwupd_remote_get_age (remote);
	if (kind == FWUPD_REMOTE_KIND_DOWNLOAD &&
		age > 0 && age != G_MAXUINT64) {
		const gchar *unit = "s";
		g_autofree gchar *age_str = NULL;
		if (age > 60) {
			age /= 60.f;
			unit = "m";
		}
		if (age > 60) {
			age /= 60.f;
			unit = "h";
		}
		if (age > 24) {
			age /= 24.f;
			unit = "d";
		}
		if (age > 7) {
			age /= 7.f;
			unit = "w";
		}
		age_str = g_strdup_printf ("%.2f%s", age, unit);
		/* TRANSLATORS: the age of the metadata */
		fu_common_string_append_kv (str, idt + 1, _("Age"), age_str);
	}
	priority = fwupd_remote_get_priority (remote);
	if (priority != 0) {
		g_autofree gchar *priority_str = NULL;
		priority_str = g_strdup_printf ("%i", priority);
		/* TRANSLATORS: the numeric priority */
		fu_common_string_append_kv (str, idt + 1, _("Priority"), priority_str);
	}
	tmp = fwupd_remote_get_username (remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote filename base */
		fu_common_string_append_kv (str, idt + 1, _("Username"), tmp);
	}
	tmp = fwupd_remote_get_password (remote);
	if (tmp != NULL) {
		g_autofree gchar *hidden = g_strnfill (strlen (tmp), '*');
		/* TRANSLATORS: remote filename base */
		fu_common_string_append_kv (str, idt + 1, _("Password"), hidden);
	}
	tmp = fwupd_remote_get_filename_cache (remote);
	if (tmp != NULL) {
		/* TRANSLATORS: filename of the local file */
		fu_common_string_append_kv (str, idt + 1, _("Filename"), tmp);
	}
	tmp = fwupd_remote_get_filename_cache_sig (remote);
	if (tmp != NULL) {
		/* TRANSLATORS: filename of the local file */
		fu_common_string_append_kv (str, idt + 1, _("Filename Signature"), tmp);
	}
	tmp = fwupd_remote_get_metadata_uri (remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote URI */
		fu_common_string_append_kv (str, idt + 1, _("Metadata URI"), tmp);
	}
	tmp = fwupd_remote_get_metadata_uri_sig (remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote URI */
		fu_common_string_append_kv (str, idt + 1, _("Metadata Signature"), tmp);
	}
	tmp = fwupd_remote_get_firmware_base_uri (remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote URI */
		fu_common_string_append_kv (str, idt + 1, _("Firmware Base URI"), tmp);
	}
	tmp = fwupd_remote_get_report_uri (remote);
	if (tmp != NULL) {
		/* TRANSLATORS: URI to send success/failure reports */
		fu_common_string_append_kv (str, idt + 1, _("Report URI"), tmp);
	}

	return g_string_free (str, FALSE);
}
