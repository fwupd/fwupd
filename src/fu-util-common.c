/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <config.h>

#include <stdio.h>
#include <glib/gi18n.h>
#include <gusb.h>

#include "fu-util-common.h"
#include "fu-device.h"

#define SYSTEMD_SERVICE			"org.freedesktop.systemd1"
#define SYSTEMD_OBJECT_PATH		"/org/freedesktop/systemd1"
#define SYSTEMD_MANAGER_INTERFACE	"org.freedesktop.systemd1.Manager"
#define SYSTEMD_FWUPD_UNIT		"fwupd.service"

gboolean
fu_util_stop_daemon (GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;

	/* try to stop any already running daemon */
	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL) {
		g_prefix_error (error, "failed to get bus: ");
		return FALSE;
	}
	proxy = g_dbus_proxy_new_sync (connection,
					G_DBUS_PROXY_FLAGS_NONE,
					NULL,
					SYSTEMD_SERVICE,
					SYSTEMD_OBJECT_PATH,
					SYSTEMD_MANAGER_INTERFACE,
					NULL,
					error);
	if (proxy == NULL) {
		g_prefix_error (error, "failed to find %s: ", SYSTEMD_SERVICE);
		return FALSE;
	}
	val = g_dbus_proxy_call_sync (proxy,
				      "GetUnit",
				      g_variant_new ("(s)",
						     SYSTEMD_FWUPD_UNIT),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      error);
	if (val == NULL) {
		g_prefix_error (error, "failed to find %s: ", SYSTEMD_FWUPD_UNIT);
		return FALSE;
	}
	g_variant_unref (val);
	val = g_dbus_proxy_call_sync (proxy,
				      "StopUnit",
				      g_variant_new ("(ss)",
						     SYSTEMD_FWUPD_UNIT,
						     "replace"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      error);
	return val != NULL;
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

gboolean
fu_util_print_device_tree (GNode *n, gpointer data)
{
	FwupdDevice *dev = FWUPD_DEVICE (n->data);
	const gchar *name;
	g_autoptr(GString) str = g_string_new (NULL);

	/* root node */
	if (dev == NULL) {
		g_print ("○\n");
		return FALSE;
	}

	/* add previous branches */
	for (GNode *c = n->parent; c->parent != NULL; c = c->parent) {
		if (g_node_next_sibling (c) == NULL)
			g_string_prepend (str, "  ");
		else
			g_string_prepend (str, "│ ");
	}

	/* add this branch */
	if (g_node_last_sibling (n) == n)
		g_string_append (str, "└─ ");
	else
		g_string_append (str, "├─ ");

	/* dump to the console */
	name = fwupd_device_get_name (dev);
	if (name == NULL)
		name = "Unknown device";
	g_string_append (str, name);
	for (guint i = strlen (name) + 2 * g_node_depth (n); i < 45; i++)
		g_string_append_c (str, ' ');
	g_print ("%s %s\n", str->str, fu_device_get_id (dev));
	return FALSE;
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
	g_autofree gchar *basename = g_path_get_basename (fn);
	g_autofree gchar *cachedir_legacy = NULL;

	/* return the legacy path if it exists rather than renaming it to
	 * prevent problems when using old and new versions of fwupd */
	cachedir_legacy = g_build_filename (g_get_user_cache_dir (), "fwupdmgr", NULL);
	if (g_file_test (cachedir_legacy, G_FILE_TEST_IS_DIR))
		return g_build_filename (cachedir_legacy, basename, NULL);

	return g_build_filename (g_get_user_cache_dir (), "fwupd", basename, NULL);
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
	if (http_proxy != NULL) {
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
		if (g_strcmp0 (cat, "X-Controller") == 0) {
			/* TRANSLATORS: the controller is a device that has other devices
			 * plugged into it, for example ThunderBolt, FireWire or USB,
			 * the first %s is the device name, e.g. 'Intel ThunderBolt` */
			return g_strdup_printf (_("%s Controller Update"), name);
		}
	}

	/* TRANSLATORS: this is the fallback where we don't know if the release
	 * is updating the system, the device, or a device class, or something else --
	 * the first %s is the device name, e.g. 'ThinkPad P50` */
	return g_strdup_printf (_("%s Update"), name);
}
