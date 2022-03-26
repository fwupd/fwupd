/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuMain"

#include <config.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <unistd.h>
#ifdef HAVE_GUSB
#include <gusb.h>
#endif
#include <fwupd.h>
#include <xmlb.h>
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#endif

#include "fu-common.h"
#include "fu-device-private.h"
#include "fu-device.h"
#include "fu-security-attr.h"
#include "fu-security-attrs.h"
#include "fu-util-common.h"

#ifdef HAVE_SYSTEMD
#include "fu-systemd.h"
#endif

#define SYSTEMD_FWUPD_UNIT	"fwupd.service"
#define SYSTEMD_SNAP_FWUPD_UNIT "snap.fwupd.fwupd.service"

const gchar *
fu_util_get_systemd_unit(void)
{
	if (g_getenv("SNAP") != NULL)
		return SYSTEMD_SNAP_FWUPD_UNIT;
	return SYSTEMD_FWUPD_UNIT;
}

gchar *
fu_util_term_format(const gchar *text, FuUtilTermColor fg_color)
{
	return g_strdup_printf("\033[%um\033[1m%s\033[0m", fg_color, text);
}

#ifdef HAVE_SYSTEMD
static const gchar *
fu_util_get_expected_command(const gchar *target)
{
	if (g_strcmp0(target, SYSTEMD_SNAP_FWUPD_UNIT) == 0)
		return "fwupd.fwupdmgr";
	return "fwupdmgr";
}
#endif

gboolean
fu_util_using_correct_daemon(GError **error)
{
#ifdef HAVE_SYSTEMD
	g_autofree gchar *default_target = NULL;
	g_autoptr(GError) error_local = NULL;
	const gchar *target;

	if (g_getenv("FWUPD_DBUS_SOCKET") != NULL)
		return TRUE;

	target = fu_util_get_systemd_unit();

	default_target = fu_systemd_get_default_target(&error_local);
	if (default_target == NULL) {
		g_debug("Systemd isn't accessible: %s\n", error_local->message);
		return TRUE;
	}
	if (!fu_systemd_unit_check_exists(target, &error_local)) {
		g_debug("wrong target: %s\n", error_local->message);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    /* TRANSLATORS: error message */
			    _("Mismatched daemon and client, use %s instead"),
			    fu_util_get_expected_command(target));
		return FALSE;
	}
#endif
	return TRUE;
}

void
fu_util_print_data(const gchar *title, const gchar *msg)
{
	gsize title_len;
	g_auto(GStrv) lines = NULL;

	if (msg == NULL)
		return;
	g_print("%s:", title);

	/* pad */
	title_len = fu_common_strwidth(title) + 1;
	lines = g_strsplit(msg, "\n", -1);
	for (guint j = 0; lines[j] != NULL; j++) {
		for (gsize i = title_len; i < 25; i++)
			g_print(" ");
		g_print("%s\n", lines[j]);
		title_len = 0;
	}
}

guint
fu_util_prompt_for_number(guint maxnum)
{
	gint retval;
	guint answer = 0;

	do {
		char buffer[64];

		/* swallow the \n at end of line too */
		if (!fgets(buffer, sizeof(buffer), stdin))
			break;
		if (strlen(buffer) == sizeof(buffer) - 1)
			continue;

		/* get a number */
		retval = sscanf(buffer, "%u", &answer);

		/* positive */
		if (retval == 1 && answer <= maxnum)
			break;

		/* TRANSLATORS: the user isn't reading the question */
		g_print(_("Please enter a number from 0 to %u: "), maxnum);
	} while (TRUE);
	return answer;
}

gboolean
fu_util_prompt_for_boolean(gboolean def)
{
	do {
		char buffer[4];
		if (!fgets(buffer, sizeof(buffer), stdin))
			continue;
		if (strlen(buffer) == sizeof(buffer) - 1)
			continue;
		if (g_strcmp0(buffer, "\n") == 0)
			return def;
		buffer[0] = g_ascii_toupper(buffer[0]);
		if (g_strcmp0(buffer, "Y\n") == 0)
			return TRUE;
		if (g_strcmp0(buffer, "N\n") == 0)
			return FALSE;
	} while (TRUE);
	return FALSE;
}

static gboolean
fu_util_traverse_tree(GNode *n, gpointer data)
{
	guint idx = g_node_depth(n) - 1;
	g_autofree gchar *tmp = NULL;
	g_auto(GStrv) split = NULL;

	/* get split lines */
	if (FWUPD_IS_DEVICE(n->data)) {
		FwupdDevice *dev = FWUPD_DEVICE(n->data);
		tmp = fu_util_device_to_string(dev, idx);
	} else if (FWUPD_IS_REMOTE(n->data)) {
		FwupdRemote *remote = FWUPD_REMOTE(n->data);
		tmp = fu_util_remote_to_string(remote, idx);
	} else if (FWUPD_IS_RELEASE(n->data)) {
		FwupdRelease *release = FWUPD_RELEASE(n->data);
		tmp = fu_util_release_to_string(release, idx);
		g_debug("%s", tmp);
	}

	/* root node */
	if (n->data == NULL && g_getenv("FWUPD_VERBOSE") == NULL) {
		const gchar *str = data;
		g_print("%s\n│\n", str != NULL ? str : "○");
		return FALSE;
	}

	if (n->parent == NULL)
		return FALSE;

	if (tmp == NULL)
		return FALSE;
	split = g_strsplit(tmp, "\n", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		g_autoptr(GString) str = g_string_new(NULL);

		/* header */
		if (i == 0) {
			if (g_node_next_sibling(n) == NULL)
				g_string_prepend(str, "└─");
			else
				g_string_prepend(str, "├─");

			/* properties */
		} else {
			g_string_prepend(str, n->children == NULL ? "  " : " │");
			g_string_prepend(str, g_node_next_sibling(n) == NULL ? " " : "│");
			g_string_append(str, " ");
		}

		/* ancestors */
		for (GNode *c = n->parent; c->parent != NULL; c = c->parent) {
			if (g_node_next_sibling(c) != NULL || idx == 0) {
				g_string_prepend(str, "│ ");
				continue;
			}
			g_string_prepend(str, "  ");
		}

		/* empty line */
		if (split[i][0] == '\0') {
			g_print("%s\n", str->str);
			continue;
		}

		/* dump to the console */
		g_string_append(str, split[i] + (idx * 2));
		g_print("%s\n", str->str);
	}

	return FALSE;
}

void
fu_util_print_tree(GNode *n, gpointer data)
{
	g_node_traverse(n, G_PRE_ORDER, G_TRAVERSE_ALL, -1, fu_util_traverse_tree, data);
}

static gboolean
fu_util_is_interesting_child(FwupdDevice *dev)
{
	GPtrArray *children = fwupd_device_get_children(dev);
	for (guint i = 0; i < children->len; i++) {
		FwupdDevice *child = g_ptr_array_index(children, i);
		if (fu_util_is_interesting_device(child))
			return TRUE;
	}
	return FALSE;
}

gboolean
fu_util_is_interesting_device(FwupdDevice *dev)
{
	if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE))
		return TRUE;
	if (fwupd_device_get_update_error(dev) != NULL)
		return TRUE;
	/* device not plugged in, get-details */
	if (fwupd_device_get_flags(dev) == 0)
		return TRUE;
	if (fu_util_is_interesting_child(dev))
		return TRUE;
	return FALSE;
}

gchar *
fu_util_get_user_cache_path(const gchar *fn)
{
	const gchar *root = g_get_user_cache_dir();
	g_autofree gchar *basename = g_path_get_basename(fn);
	g_autofree gchar *cachedir_legacy = NULL;

	/* if run from a systemd unit, use the cache directory set there */
	if (g_getenv("CACHE_DIRECTORY") != NULL)
		root = g_getenv("CACHE_DIRECTORY");

	/* return the legacy path if it exists rather than renaming it to
	 * prevent problems when using old and new versions of fwupd */
	cachedir_legacy = g_build_filename(root, "fwupdmgr", NULL);
	if (g_file_test(cachedir_legacy, G_FILE_TEST_IS_DIR))
		return g_build_filename(cachedir_legacy, basename, NULL);

	return g_build_filename(root, "fwupd", basename, NULL);
}

static gboolean
fu_util_update_shutdown(GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;

#ifdef HAVE_LOGIND
	/* shutdown using logind */
	val = g_dbus_connection_call_sync(connection,
					  "org.freedesktop.login1",
					  "/org/freedesktop/login1",
					  "org.freedesktop.login1.Manager",
					  "PowerOff",
					  g_variant_new("(b)", TRUE),
					  NULL,
					  G_DBUS_CALL_FLAGS_NONE,
					  -1,
					  NULL,
					  error);
#elif defined(HAVE_CONSOLEKIT)
	/* shutdown using ConsoleKit */
	val = g_dbus_connection_call_sync(connection,
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
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    "No supported backend compiled in to perform the operation.");
#endif
	return val != NULL;
}

gboolean
fu_util_update_reboot(GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;

#ifdef HAVE_LOGIND
	/* reboot using logind */
	val = g_dbus_connection_call_sync(connection,
					  "org.freedesktop.login1",
					  "/org/freedesktop/login1",
					  "org.freedesktop.login1.Manager",
					  "Reboot",
					  g_variant_new("(b)", TRUE),
					  NULL,
					  G_DBUS_CALL_FLAGS_NONE,
					  -1,
					  NULL,
					  error);
#elif defined(HAVE_CONSOLEKIT)
	/* reboot using ConsoleKit */
	val = g_dbus_connection_call_sync(connection,
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
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    "No supported backend compiled in to perform the operation.");
#endif
	return val != NULL;
}

static gchar *
fu_util_get_release_description_with_fallback(FwupdRelease *rel)
{
	g_autoptr(GString) str = g_string_new(NULL);

	/* add what we've got from the vendor */
	if (fwupd_release_get_description(rel) != NULL)
		g_string_append(str, fwupd_release_get_description(rel));

	/* add this client side to get the translations */
	if (fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_IS_COMMUNITY)) {
		g_string_append_printf(
		    str,
		    "<p>%s</p>",
		    /* TRANSLATORS: the vendor did not upload this */
		    _("This firmware is provided by LVFS community members and is not "
		      "provided (or supported) by the original hardware vendor."));
		g_string_append_printf(
		    str,
		    "<p>%s</p>",
		    /* TRANSLATORS: if it breaks, you get to keep both pieces */
		    _("Installing this update may also void any device warranty."));
	}

	/* this can't be from the LVFS, but the user could be installing a local file */
	if (str->len == 0) {
		g_string_append_printf(str,
				       "<p>%s</p>",
				       /* TRANSLATORS: naughty vendor */
				       _("The vendor did not supply any release notes."));
	}

	return g_string_free(g_steal_pointer(&str), FALSE);
}

gboolean
fu_util_prompt_warning(FwupdDevice *device,
		       FwupdRelease *release,
		       const gchar *machine,
		       GError **error)
{
	FwupdDeviceFlags flags;
	gint vercmp;
	g_autofree gchar *desc_fb = NULL;
	g_autoptr(GString) title = g_string_new(NULL);
	g_autoptr(GString) str = g_string_new(NULL);

	/* up, down, or re-install */
	vercmp = fu_common_vercmp_full(fwupd_release_get_version(release),
				       fu_device_get_version(device),
				       fwupd_device_get_version_format(device));
	if (vercmp < 0) {
		g_string_append_printf(
		    title,
		    /* TRANSLATORS: message letting the user know an downgrade is available
		     * %1 is the device name and %2 and %3 are version strings */
		    _("Downgrade %s from %s to %s?"),
		    fwupd_device_get_name(device),
		    fwupd_device_get_version(device),
		    fwupd_release_get_version(release));
	} else if (vercmp > 0) {
		g_string_append_printf(
		    title,
		    /* TRANSLATORS: message letting the user know an upgrade is available
		     * %1 is the device name and %2 and %3 are version strings */
		    _("Upgrade %s from %s to %s?"),
		    fwupd_device_get_name(device),
		    fwupd_device_get_version(device),
		    fwupd_release_get_version(release));
	} else {
		g_string_append_printf(
		    title,
		    /* TRANSLATORS: message letting the user know an upgrade is available
		     * %1 is the device name and %2 is a version string */
		    _("Reinstall %s to %s?"),
		    fwupd_device_get_name(device),
		    fwupd_release_get_version(release));
	}

	/* description is optional */
	desc_fb = fu_util_get_release_description_with_fallback(release);
	if (desc_fb != NULL) {
		g_autofree gchar *desc = fu_util_convert_description(desc_fb, NULL);
		if (desc != NULL)
			g_string_append_printf(str, "\n%s", desc);
	}

	/* device is not already in bootloader mode so show warning */
	flags = fwupd_device_get_flags(device);
	if ((flags & FWUPD_DEVICE_FLAG_IS_BOOTLOADER) == 0) {
		/* device may reboot */
		if ((flags & FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE) == 0) {
			g_string_append(str, "\n\n");
			g_string_append_printf(
			    str,
			    /* TRANSLATORS: warn the user before updating, %1 is a device name */
			    _("%s and all connected devices may not be usable while updating."),
			    fwupd_device_get_name(device));

			/* device can get bricked */
		} else if ((flags & FWUPD_DEVICE_FLAG_SELF_RECOVERY) == 0) {
			g_string_append(str, "\n\n");
			/* external device */
			if ((flags & FWUPD_DEVICE_FLAG_INTERNAL) == 0) {
				g_string_append_printf(str,
						       /* TRANSLATORS: warn the user before
							* updating, %1 is a device name
							*/
						       _("%s must remain connected for the "
							 "duration of the update to avoid damage."),
						       fwupd_device_get_name(device));
			} else if (flags & FWUPD_DEVICE_FLAG_REQUIRE_AC) {
				g_string_append_printf(
				    str,
				    /* TRANSLATORS: warn the user before updating, %1 is a machine
				     * name
				     */
				    _("%s must remain plugged into a power source for the duration "
				      "of the update to avoid damage."),
				    machine);
			}
		}
	}
	fu_util_warning_box(title->str, str->str, 80);

	/* ask for confirmation */
	g_print("\n%s [Y|n]: ",
		/* TRANSLATORS: prompt to apply the update */
		_("Perform operation?"));
	if (!fu_util_prompt_for_boolean(TRUE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_util_prompt_complete(FwupdDeviceFlags flags, gboolean prompt, GError **error)
{
	if (flags & FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN) {
		if (prompt) {
			g_print("\n%s %s [y|N]: ",
				/* TRANSLATORS: explain why we want to shutdown */
				_("An update requires the system to shutdown to complete."),
				/* TRANSLATORS: shutdown to apply the update */
				_("Shutdown now?"));
			if (!fu_util_prompt_for_boolean(FALSE))
				return TRUE;
		}
		return fu_util_update_shutdown(error);
	}
	if (flags & FWUPD_DEVICE_FLAG_NEEDS_REBOOT) {
		if (prompt) {
			g_print("\n%s %s [y|N]: ",
				/* TRANSLATORS: explain why we want to reboot */
				_("An update requires a reboot to complete."),
				/* TRANSLATORS: reboot to apply the update */
				_("Restart now?"));
			if (!fu_util_prompt_for_boolean(FALSE))
				return TRUE;
		}
		return fu_util_update_reboot(error);
	}

	return TRUE;
}

static void
fu_util_cmd_free(FuUtilCmd *item)
{
	g_free(item->name);
	g_free(item->arguments);
	g_free(item->description);
	g_free(item);
}

GPtrArray *
fu_util_cmd_array_new(void)
{
	return g_ptr_array_new_with_free_func((GDestroyNotify)fu_util_cmd_free);
}

static gint
fu_util_cmd_sort_cb(FuUtilCmd **item1, FuUtilCmd **item2)
{
	return g_strcmp0((*item1)->name, (*item2)->name);
}

void
fu_util_cmd_array_sort(GPtrArray *array)
{
	g_ptr_array_sort(array, (GCompareFunc)fu_util_cmd_sort_cb);
}

void
fu_util_cmd_array_add(GPtrArray *array,
		      const gchar *name,
		      const gchar *arguments,
		      const gchar *description,
		      FuUtilCmdFunc callback)
{
	g_auto(GStrv) names = NULL;

	g_return_if_fail(name != NULL);
	g_return_if_fail(description != NULL);
	g_return_if_fail(callback != NULL);

	/* add each one */
	names = g_strsplit(name, ",", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		FuUtilCmd *item = g_new0(FuUtilCmd, 1);
		item->name = g_strdup(names[i]);
		if (i == 0) {
			item->description = g_strdup(description);
		} else {
			/* TRANSLATORS: this is a command alias, e.g. 'get-devices' */
			item->description = g_strdup_printf(_("Alias to %s"), names[0]);
		}
		item->arguments = g_strdup(arguments);
		item->callback = callback;
		g_ptr_array_add(array, item);
	}
}

gboolean
fu_util_cmd_array_run(GPtrArray *array,
		      FuUtilPrivate *priv,
		      const gchar *command,
		      gchar **values,
		      GError **error)
{
	g_auto(GStrv) values_copy = g_new0(gchar *, g_strv_length(values) + 1);

	/* clear out bash completion sentinel */
	for (guint i = 0; values[i] != NULL; i++) {
		if (g_strcmp0(values[i], "{") == 0)
			break;
		values_copy[i] = g_strdup(values[i]);
	}

	/* find command */
	for (guint i = 0; i < array->len; i++) {
		FuUtilCmd *item = g_ptr_array_index(array, i);
		if (g_strcmp0(item->name, command) == 0)
			return item->callback(priv, values_copy, error);
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
fu_util_cmd_array_to_string(GPtrArray *array)
{
	gsize len;
	const gsize max_len = 35;
	GString *string;

	/* print each command */
	string = g_string_new("");
	for (guint i = 0; i < array->len; i++) {
		FuUtilCmd *item = g_ptr_array_index(array, i);
		g_string_append(string, "  ");
		g_string_append(string, item->name);
		len = fu_common_strwidth(item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append(string, " ");
			g_string_append(string, item->arguments);
			len += fu_common_strwidth(item->arguments) + 1;
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

const gchar *
fu_util_branch_for_display(const gchar *branch)
{
	if (branch == NULL) {
		/* TRANSLATORS: this is the default branch name when unset */
		return _("default");
	}
	return branch;
}

gchar *
fu_util_release_get_name(FwupdRelease *release)
{
	const gchar *name = fwupd_release_get_name(release);
	GPtrArray *cats = fwupd_release_get_categories(release);

	for (guint i = 0; i < cats->len; i++) {
		const gchar *cat = g_ptr_array_index(cats, i);
		if (g_strcmp0(cat, "X-Device") == 0) {
			/* TRANSLATORS: a specific part of hardware,
			 * the first %s is the device name, e.g. 'Unifying Receiver` */
			return g_strdup_printf(_("%s Device Update"), name);
		}
		if (g_strcmp0(cat, "X-Configuration") == 0) {
			/* TRANSLATORS: a specific part of hardware,
			 * the first %s is the device name, e.g. 'Secure Boot` */
			return g_strdup_printf(_("%s Configuration Update"), name);
		}
		if (g_strcmp0(cat, "X-System") == 0) {
			/* TRANSLATORS: the entire system, e.g. all internal devices,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s System Update"), name);
		}
		if (g_strcmp0(cat, "X-EmbeddedController") == 0) {
			/* TRANSLATORS: the EC is typically the keyboard controller chip,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s Embedded Controller Update"), name);
		}
		if (g_strcmp0(cat, "X-ManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine, the Intel AMT thing,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s ME Update"), name);
		}
		if (g_strcmp0(cat, "X-CorporateManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine (with Intel AMT),
			 * where the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s Corporate ME Update"), name);
		}
		if (g_strcmp0(cat, "X-ConsumerManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine, where
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s Consumer ME Update"), name);
		}
		if (g_strcmp0(cat, "X-Controller") == 0) {
			/* TRANSLATORS: the controller is a device that has other devices
			 * plugged into it, for example ThunderBolt, FireWire or USB,
			 * the first %s is the device name, e.g. 'Intel ThunderBolt` */
			return g_strdup_printf(_("%s Controller Update"), name);
		}
		if (g_strcmp0(cat, "X-ThunderboltController") == 0) {
			/* TRANSLATORS: the Thunderbolt controller is a device that
			 * has other high speed Thunderbolt devices plugged into it;
			 * the first %s is the system name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s Thunderbolt Controller Update"), name);
		}
		if (g_strcmp0(cat, "X-CpuMicrocode") == 0) {
			/* TRANSLATORS: the CPU microcode is firmware loaded onto the CPU
			 * at system bootup */
			return g_strdup_printf(_("%s CPU Microcode Update"), name);
		}
		if (g_strcmp0(cat, "X-Battery") == 0) {
			/* TRANSLATORS: battery refers to the system power source */
			return g_strdup_printf(_("%s Battery Update"), name);
		}
		if (g_strcmp0(cat, "X-Camera") == 0) {
			/* TRANSLATORS: camera can refer to the laptop internal
			 * camera in the bezel or external USB webcam */
			return g_strdup_printf(_("%s Camera Update"), name);
		}
		if (g_strcmp0(cat, "X-TPM") == 0) {
			/* TRANSLATORS: TPM refers to a Trusted Platform Module */
			return g_strdup_printf(_("%s TPM Update"), name);
		}
		if (g_strcmp0(cat, "X-Touchpad") == 0) {
			/* TRANSLATORS: TouchPad refers to a flat input device */
			return g_strdup_printf(_("%s Touchpad Update"), name);
		}
		if (g_strcmp0(cat, "X-Mouse") == 0) {
			/* TRANSLATORS: Mouse refers to a handheld input device */
			return g_strdup_printf(_("%s Mouse Update"), name);
		}
		if (g_strcmp0(cat, "X-Keyboard") == 0) {
			/* TRANSLATORS: Keyboard refers to an input device for typing */
			return g_strdup_printf(_("%s Keyboard Update"), name);
		}
		if (g_strcmp0(cat, "X-StorageController") == 0) {
			/* TRANSLATORS: Storage Controller is typically a RAID or SAS adapter */
			return g_strdup_printf(_("%s Storage Controller Update"), name);
		}
		if (g_strcmp0(cat, "X-NetworkInterface") == 0) {
			/* TRANSLATORS: Network Interface refers to the physical
			 * PCI card, not the logical wired connection */
			return g_strdup_printf(_("%s Network Interface Update"), name);
		}
		if (g_strcmp0(cat, "X-VideoDisplay") == 0) {
			/* TRANSLATORS: Video Display refers to the laptop internal display or
			 * external monitor */
			return g_strdup_printf(_("%s Display Update"), name);
		}
		if (g_strcmp0(cat, "X-BaseboardManagementController") == 0) {
			/* TRANSLATORS: BMC refers to baseboard management controller which
			 * is the device that updates all the other firmware on the system */
			return g_strdup_printf(_("%s BMC Update"), name);
		}
	}

	/* TRANSLATORS: this is the fallback where we don't know if the release
	 * is updating the system, the device, or a device class, or something else --
	 * the first %s is the device name, e.g. 'ThinkPad P50` */
	return g_strdup_printf(_("%s Update"), name);
}

static GPtrArray *
fu_util_strsplit_words(const gchar *text, guint line_len)
{
	g_auto(GStrv) tokens = NULL;
	g_autoptr(GPtrArray) lines = g_ptr_array_new();
	g_autoptr(GString) curline = g_string_new(NULL);

	/* sanity check */
	if (text == NULL || text[0] == '\0')
		return NULL;
	if (line_len == 0)
		return NULL;

	/* tokenize the string */
	tokens = g_strsplit(text, " ", -1);
	for (guint i = 0; tokens[i] != NULL; i++) {
		/* current line plus new token is okay */
		if (curline->len + fu_common_strwidth(tokens[i]) < line_len) {
			g_string_append_printf(curline, "%s ", tokens[i]);
			continue;
		}

		/* too long, so remove space, add newline and dump */
		if (curline->len > 0)
			g_string_truncate(curline, curline->len - 1);
		g_ptr_array_add(lines, g_strdup(curline->str));
		g_string_truncate(curline, 0);
		g_string_append_printf(curline, "%s ", tokens[i]);
	}

	/* any incomplete line? */
	if (curline->len > 0) {
		g_string_truncate(curline, curline->len - 1);
		g_ptr_array_add(lines, g_strdup(curline->str));
	}
	return g_steal_pointer(&lines);
}

static void
fu_util_warning_box_line(const gchar *start,
			 const gchar *text,
			 const gchar *end,
			 const gchar *padding,
			 guint width)
{
	guint offset = 0;
	if (start != NULL) {
		offset += fu_common_strwidth(start);
		g_print("%s", start);
	}
	if (text != NULL) {
		offset += fu_common_strwidth(text);
		g_print("%s", text);
	}
	if (end != NULL)
		offset += fu_common_strwidth(end);
	for (guint i = offset; i < width; i++)
		g_print("%s", padding);
	if (end != NULL)
		g_print("%s\n", end);
}

void
fu_util_warning_box(const gchar *title, const gchar *body, guint width)
{
	/* nothing to do */
	if (title == NULL && body == NULL)
		return;

	/* header */
	fu_util_warning_box_line("╔", NULL, "╗", "═", width);

	/* optional title */
	if (title != NULL) {
		g_autoptr(GPtrArray) lines = fu_util_strsplit_words(title, width - 4);
		for (guint j = 0; j < lines->len; j++) {
			const gchar *line = g_ptr_array_index(lines, j);
			fu_util_warning_box_line("║ ", line, " ║", " ", width);
		}
	}

	/* join */
	if (title != NULL && body != NULL)
		fu_util_warning_box_line("╠", NULL, "╣", "═", width);

	/* optional body */
	if (body != NULL) {
		gboolean has_nonempty = FALSE;
		g_auto(GStrv) split = g_strsplit(body, "\n", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			g_autoptr(GPtrArray) lines = fu_util_strsplit_words(split[i], width - 4);
			if (lines == NULL) {
				if (has_nonempty) {
					fu_util_warning_box_line("║ ", NULL, " ║", " ", width);
					has_nonempty = FALSE;
				}
				continue;
			}
			for (guint j = 0; j < lines->len; j++) {
				const gchar *line = g_ptr_array_index(lines, j);
				fu_util_warning_box_line("║ ", line, " ║", " ", width);
			}
			has_nonempty = TRUE;
		}
	}

	/* footer */
	fu_util_warning_box_line("╚", NULL, "╝", "═", width);
}

gboolean
fu_util_parse_filter_flags(const gchar *filter,
			   FwupdDeviceFlags *include,
			   FwupdDeviceFlags *exclude,
			   GError **error)
{
	FwupdDeviceFlags tmp;
	g_auto(GStrv) strv = g_strsplit(filter, ",", -1);

	g_return_val_if_fail(include != NULL, FALSE);
	g_return_val_if_fail(exclude != NULL, FALSE);

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
			if ((tmp & *include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & *exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			*exclude |= tmp;
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
			if ((tmp & *exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & *include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			*include |= tmp;
		}
	}

	return TRUE;
}

typedef struct {
	guint cnt;
	GString *str;
} FuUtilConvertHelper;

static gboolean
fu_util_convert_description_head_cb(XbNode *n, gpointer user_data)
{
	FuUtilConvertHelper *helper = (FuUtilConvertHelper *)user_data;
	helper->cnt++;

	/* start */
	if (g_strcmp0(xb_node_get_element(n), "em") == 0) {
		g_string_append(helper->str, "\033[3m");
	} else if (g_strcmp0(xb_node_get_element(n), "strong") == 0) {
		g_string_append(helper->str, "\033[1m");
	} else if (g_strcmp0(xb_node_get_element(n), "code") == 0) {
		g_string_append(helper->str, "`");
	} else if (g_strcmp0(xb_node_get_element(n), "li") == 0) {
		g_string_append(helper->str, "• ");
	} else if (g_strcmp0(xb_node_get_element(n), "p") == 0 ||
		   g_strcmp0(xb_node_get_element(n), "ul") == 0 ||
		   g_strcmp0(xb_node_get_element(n), "ol") == 0) {
		g_string_append(helper->str, "\n");
	}

	/* text */
	if (xb_node_get_text(n) != NULL)
		g_string_append(helper->str, xb_node_get_text(n));

	return FALSE;
}

static gboolean
fu_util_convert_description_tail_cb(XbNode *n, gpointer user_data)
{
	FuUtilConvertHelper *helper = (FuUtilConvertHelper *)user_data;
	helper->cnt++;

	/* end */
	if (g_strcmp0(xb_node_get_element(n), "em") == 0 ||
	    g_strcmp0(xb_node_get_element(n), "strong") == 0) {
		g_string_append(helper->str, "\033[0m");
	} else if (g_strcmp0(xb_node_get_element(n), "code") == 0) {
		g_string_append(helper->str, "`");
	} else if (g_strcmp0(xb_node_get_element(n), "li") == 0) {
		g_string_append(helper->str, "\n");
	} else if (g_strcmp0(xb_node_get_element(n), "p") == 0) {
		g_string_append(helper->str, "\n");
	}

	/* tail */
	if (xb_node_get_tail(n) != NULL)
		g_string_append(helper->str, xb_node_get_tail(n));

	return FALSE;
}

gchar *
fu_util_convert_description(const gchar *xml, GError **error)
{
	g_autoptr(GString) str = g_string_new(NULL);
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbSilo) silo = NULL;
	FuUtilConvertHelper helper = {
	    .cnt = 0,
	    .str = str,
	};

	/* parse XML */
	silo = xb_silo_new_from_xml(xml, error);
	if (silo == NULL)
		return NULL;

	/* convert to something we can show on the console */
	n = xb_silo_get_root(silo);
	xb_node_transmogrify(n,
			     fu_util_convert_description_head_cb,
			     fu_util_convert_description_tail_cb,
			     &helper);

	/* success */
	return fu_common_strstrip(str->str);
}

/**
 * fu_util_time_to_str:
 * @tmp: the time in seconds
 *
 * Converts a timestamp to a 'pretty' translated string
 *
 * Returns: (transfer full): A string
 *
 * Since: 1.3.7
 **/
gchar *
fu_util_time_to_str(guint64 tmp)
{
	g_return_val_if_fail(tmp != 0, NULL);

	/* seconds */
	if (tmp < 60) {
		/* TRANSLATORS: duration in seconds */
		return g_strdup_printf(ngettext("%u second", "%u seconds", (gint)tmp), (guint)tmp);
	}

	/* minutes */
	tmp /= 60;
	if (tmp < 60) {
		/* TRANSLATORS: duration in minutes */
		return g_strdup_printf(ngettext("%u minute", "%u minutes", (gint)tmp), (guint)tmp);
	}

	/* hours */
	tmp /= 60;
	if (tmp < 60) {
		/* TRANSLATORS: duration in minutes */
		return g_strdup_printf(ngettext("%u hour", "%u hours", (gint)tmp), (guint)tmp);
	}

	/* days */
	tmp /= 24;
	/* TRANSLATORS: duration in days! */
	return g_strdup_printf(ngettext("%u day", "%u days", (gint)tmp), (guint)tmp);
}

static gchar *
fu_util_device_flag_to_string(guint64 device_flag)
{
	if (device_flag == FWUPD_DEVICE_FLAG_NONE) {
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_INTERNAL) {
		/* TRANSLATORS: Device cannot be removed easily*/
		return _("Internal device");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_UPDATABLE ||
	    device_flag == FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN) {
		/* TRANSLATORS: Device is updatable in this or any other mode */
		return _("Updatable");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_ONLY_OFFLINE) {
		/* TRANSLATORS: Update can only be done from offline mode */
		return _("Update requires a reboot");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_REQUIRE_AC) {
		/* TRANSLATORS: Must be plugged in to an outlet */
		return _("System requires external power source");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_LOCKED) {
		/* TRANSLATORS: Is locked and can be unlocked */
		return _("Device is locked");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_SUPPORTED) {
		/* TRANSLATORS: Is found in current metadata */
		return _("Supported on remote server");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER) {
		/* TRANSLATORS: Requires a bootloader mode to be manually enabled by the user */
		return _("Requires a bootloader");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_REBOOT) {
		/* TRANSLATORS: Requires a reboot to apply firmware or to reload hardware */
		return _("Needs a reboot after installation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN) {
		/* TRANSLATORS: Requires system shutdown to apply firmware */
		return _("Needs shutdown after installation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_REPORTED) {
		/* TRANSLATORS: Has been reported to a metadata server */
		return _("Reported to remote server");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NOTIFIED) {
		/* TRANSLATORS: User has been notified */
		return _("User has been notified");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST) {
		/* TRANSLATORS: Install composite firmware on the parent before the child */
		return _("Install to parent device first");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_IS_BOOTLOADER) {
		/* TRANSLATORS: Is currently in bootloader mode */
		return _("Is in bootloader mode");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG) {
		/* TRANSLATORS: the hardware is waiting to be replugged */
		return _("Hardware is waiting to be replugged");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_IGNORE_VALIDATION) {
		/* TRANSLATORS: Ignore validation safety checks when flashing this device */
		return _("Ignore validation safety checks");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION) {
		/* TRANSLATORS: Device update needs to be separately activated */
		return _("Device update needs activation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_HISTORICAL) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_WILL_DISAPPEAR) {
		/* TRANSLATORS: Device will not return after update completes */
		return _("Device will not re-appear after update completes");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_VERIFY) {
		/* TRANSLATORS: Device supports some form of checksum verification */
		return _("Cryptographic hash verification is available");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_DUAL_IMAGE) {
		/* TRANSLATORS: Device supports a safety mechanism for flashing */
		return _("Device stages updates");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_SELF_RECOVERY) {
		/* TRANSLATORS: Device supports a safety mechanism for flashing */
		return _("Device can recover flash failures");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE) {
		/* TRANSLATORS: Device remains usable during update */
		return _("Device is usable for the duration of the update");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED) {
		/* TRANSLATORS: a version check is required for all firmware */
		return _("Device firmware is required to have a version check");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES) {
		/* TRANSLATORS: the device cannot update from A->C and has to go A->B->C */
		return _("Device is required to install all provided releases");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES) {
		/* TRANSLATORS: there is more than one supplier of the firmware */
		return _("Device supports switching to a different branch of firmware");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL) {
		/* TRANSLATORS: save the old firmware to disk before installing the new one */
		return _("Device will backup firmware before installing");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_WILDCARD_INSTALL) {
		/* TRANSLATORS: on some systems certain devices have to have matching versions,
		 * e.g. the EFI driver for a given network card cannot be different */
		return _("All devices of the same type will be updated at the same time");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE) {
		/* TRANSLATORS: some devices can only be updated to a new semver and cannot
		 * be downgraded or reinstalled with the existing version */
		return _("Only version upgrades are allowed");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_UNREACHABLE) {
		/* TRANSLATORS: currently unreachable, perhaps because it is in a lower power state
		 * or is out of wireless range */
		return _("Device is unreachable");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_AFFECTS_FDE) {
		/* TRANSLATORS: we might ask the user the recovery key when next booting Windows */
		return _("Full disk encryption secrets may be invalidated when updating");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_END_OF_LIFE) {
		/* TRANSLATORS: the vendor is no longer supporting the device */
		return _("End of life");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD) {
		/* TRANSLATORS: firmware is verified on-device the payload using strong crypto */
		return _("Signed Payload");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD) {
		/* TRANSLATORS: firmware payload is unsigned and it is possible to modify it */
		return _("Unsigned Payload");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_SKIPS_RESTART) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_UNKNOWN) {
		return NULL;
	}
	return NULL;
}

static const gchar *
fu_util_update_state_to_string(FwupdUpdateState update_state)
{
	if (update_state == FWUPD_UPDATE_STATE_PENDING) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Pending");
	}
	if (update_state == FWUPD_UPDATE_STATE_SUCCESS) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Success");
	}
	if (update_state == FWUPD_UPDATE_STATE_FAILED) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Failed");
	}
	if (update_state == FWUPD_UPDATE_STATE_FAILED_TRANSIENT) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Transient failure");
	}
	if (update_state == FWUPD_UPDATE_STATE_NEEDS_REBOOT) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Needs reboot");
	}
	return NULL;
}

gchar *
fu_util_device_to_string(FwupdDevice *dev, guint idt)
{
	FwupdUpdateState state;
	GPtrArray *guids = fwupd_device_get_guids(dev);
	GPtrArray *issues = fwupd_device_get_issues(dev);
	GPtrArray *vendor_ids = fwupd_device_get_vendor_ids(dev);
	GPtrArray *instance_ids = fwupd_device_get_instance_ids(dev);
	const gchar *tmp;
	const gchar *tmp2;
	guint64 flags = fwupd_device_get_flags(dev);
	guint64 modified = fwupd_device_get_modified(dev);
	g_autoptr(GHashTable) ids = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* some fields are intentionally not included and are only shown in --verbose */
	if (g_getenv("FWUPD_VERBOSE") != NULL) {
		g_autofree gchar *debug_str = NULL;
		debug_str = fwupd_device_to_string(dev);
		g_debug("%s", debug_str);
		return NULL;
	}

	tmp = fwupd_device_get_name(dev);
	if (tmp == NULL) {
		/* TRANSLATORS: Name of hardware */
		tmp = _("Unknown Device");
	}
	fu_common_string_append_kv(str, idt, tmp, NULL);

	tmp = fwupd_device_get_id(dev);
	if (tmp != NULL) {
		/* TRANSLATORS: ID for hardware, typically a SHA1 sum */
		fu_common_string_append_kv(str, idt + 1, _("Device ID"), tmp);
	}

	/* summary */
	tmp = fwupd_device_get_summary(dev);
	if (tmp != NULL) {
		/* TRANSLATORS: one line summary of device */
		fu_common_string_append_kv(str, idt + 1, _("Summary"), tmp);
	}

	/* description */
	tmp = fwupd_device_get_description(dev);
	if (tmp != NULL) {
		g_autofree gchar *desc = NULL;
		desc = fu_util_convert_description(tmp, NULL);
		if (desc == NULL)
			desc = g_strdup(tmp);
		/* TRANSLATORS: multiline description of device */
		fu_common_string_append_kv(str, idt + 1, _("Description"), desc);
	}

	/* versions */
	tmp = fwupd_device_get_version(dev);
	if (tmp != NULL) {
		g_autoptr(GString) verstr = g_string_new(tmp);
		if (fwupd_device_get_version_build_date(dev) != 0) {
			guint64 value = fwupd_device_get_version_build_date(dev);
			g_autoptr(GDateTime) date = g_date_time_new_from_unix_utc((gint64)value);
			g_autofree gchar *datestr = g_date_time_format(date, "%F");
			g_string_append_printf(verstr, " [%s]", datestr);
		}
		if (flags & FWUPD_DEVICE_FLAG_HISTORICAL) {
			fu_common_string_append_kv(
			    str,
			    idt + 1,
			    /* TRANSLATORS: version number of previous firmware */
			    _("Previous version"),
			    verstr->str);
		} else {
			/* TRANSLATORS: version number of current firmware */
			fu_common_string_append_kv(str, idt + 1, _("Current version"), verstr->str);
		}
	}
	tmp = fwupd_device_get_version_lowest(dev);
	if (tmp != NULL) {
		/* TRANSLATORS: smallest version number installable on device */
		fu_common_string_append_kv(str, idt + 1, _("Minimum Version"), tmp);
	}
	tmp = fwupd_device_get_version_bootloader(dev);
	if (tmp != NULL) {
		/* TRANSLATORS: firmware version of bootloader */
		fu_common_string_append_kv(str, idt + 1, _("Bootloader Version"), tmp);
	}

	/* vendor */
	tmp = fwupd_device_get_vendor(dev);
	if (tmp != NULL && vendor_ids->len > 0) {
		g_autofree gchar *strv = fu_common_strjoin_array(", ", vendor_ids);
		g_autofree gchar *both = g_strdup_printf("%s (%s)", tmp, strv);
		/* TRANSLATORS: manufacturer of hardware */
		fu_common_string_append_kv(str, idt + 1, _("Vendor"), both);
	} else if (tmp != NULL) {
		/* TRANSLATORS: manufacturer of hardware */
		fu_common_string_append_kv(str, idt + 1, _("Vendor"), tmp);
	} else if (vendor_ids->len > 0) {
		g_autofree gchar *strv = fu_common_strjoin_array("|", vendor_ids);
		/* TRANSLATORS: manufacturer of hardware */
		fu_common_string_append_kv(str, idt + 1, _("Vendor"), strv);
	}

	/* branch */
	if (fwupd_device_get_branch(dev) != NULL) {
		fu_common_string_append_kv(
		    str,
		    idt + 1,
		    /* TRANSLATORS: the stream of firmware, e.g. nonfree or open-source */
		    _("Release Branch"),
		    fwupd_device_get_branch(dev));
	}

	/* install duration */
	if (fwupd_device_get_install_duration(dev) > 0) {
		g_autofree gchar *time =
		    fu_util_time_to_str(fwupd_device_get_install_duration(dev));
		/* TRANSLATORS: length of time the update takes to apply */
		fu_common_string_append_kv(str, idt + 1, _("Install Duration"), time);
	}

	/* serial # */
	tmp = fwupd_device_get_serial(dev);
	if (tmp != NULL) {
		/* TRANSLATORS: serial number of hardware */
		fu_common_string_append_kv(str, idt + 1, _("Serial Number"), tmp);
	}

	/* update state */
	state = fwupd_device_get_update_state(dev);
	if (state != FWUPD_UPDATE_STATE_UNKNOWN) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: hardware state, e.g. "pending" */
					   _("Update State"),
					   fu_util_update_state_to_string(state));

		if (state == FWUPD_UPDATE_STATE_SUCCESS) {
			tmp = fwupd_device_get_update_message(dev);
			if (tmp != NULL) {
				g_autofree gchar *color =
				    fu_util_term_format(tmp, FU_UTIL_TERM_COLOR_BLUE);
				fu_common_string_append_kv(
				    str,
				    idt + 1,
				    /* TRANSLATORS: helpful messages from last update */
				    _("Update Message"),
				    color);
			}
		}
	}
	tmp = fwupd_device_get_update_error(dev);
	if (tmp != NULL) {
		g_autofree gchar *color = fu_util_term_format(tmp, FU_UTIL_TERM_COLOR_RED);
		/* TRANSLATORS: error message from last update attempt */
		fu_common_string_append_kv(str, idt + 1, _("Update Error"), color);
	}

	/* modified date: for history devices */
	if (modified > 0) {
		g_autoptr(GDateTime) date = NULL;
		g_autofree gchar *time_str = NULL;
		date = g_date_time_new_from_unix_utc(modified);
		time_str = g_date_time_format(date, "%F %R");
		/* TRANSLATORS: the original time/date the device was modified */
		fu_common_string_append_kv(str, idt + 1, _("Last modified"), time_str);
	}

	/* all GUIDs for this hardware, with IDs if available */
	ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	for (guint i = 0; i < instance_ids->len; i++) {
		const gchar *instance_id = g_ptr_array_index(instance_ids, i);
		g_hash_table_insert(ids,
				    fwupd_guid_hash_string(instance_id),
				    g_strdup(instance_id));
	}
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		const gchar *instance_id = g_hash_table_lookup(ids, guid);
		g_autofree gchar *guid_src = NULL;

		/* instance IDs are only available as root */
		if (instance_id == NULL) {
			guid_src = g_strdup(guid);
		} else {
			guid_src = g_strdup_printf("%s ← %s", guid, instance_id);
		}
		if (i == 0) {
			fu_common_string_append_kv(
			    str,
			    idt + 1,
			    /* TRANSLATORS: global ID common to all similar hardware */
			    ngettext("GUID", "GUIDs", guids->len),
			    guid_src);
		} else {
			fu_common_string_append_kv(str, idt + 1, "", guid_src);
		}
	}

	/* TRANSLATORS: description of device ability */
	tmp = _("Device Flags");
	for (guint i = 0; i < 64; i++) {
		if ((flags & ((guint64)1 << i)) == 0)
			continue;
		tmp2 = fu_util_device_flag_to_string((guint64)1 << i);
		if (tmp2 == NULL)
			continue;
		/* header */
		if (tmp != NULL) {
			g_autofree gchar *bullet = NULL;
			bullet = g_strdup_printf("• %s", tmp2);
			fu_common_string_append_kv(str, idt + 1, tmp, bullet);
			tmp = NULL;
		} else {
			g_autofree gchar *bullet = NULL;
			bullet = g_strdup_printf("• %s", tmp2);
			fu_common_string_append_kv(str, idt + 1, "", bullet);
		}
	}
	for (guint i = 0; i < issues->len; i++) {
		const gchar *issue = g_ptr_array_index(issues, i);
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: issue fixed with the release, e.g. CVE */
					   i == 0 ? ngettext("Issue", "Issues", issues->len) : "",
					   issue);
	}

	return g_string_free(g_steal_pointer(&str), FALSE);
}

const gchar *
fu_util_plugin_flag_to_string(FwupdPluginFlags plugin_flag)
{
	if (plugin_flag == FWUPD_PLUGIN_FLAG_UNKNOWN)
		return NULL;
	if (plugin_flag == FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE)
		return NULL;
	if (plugin_flag == FWUPD_PLUGIN_FLAG_USER_WARNING)
		return NULL;
	if (plugin_flag == FWUPD_PLUGIN_FLAG_REQUIRE_HWID) {
		/* TRANSLATORS: Plugin is active only if hardware is found */
		return _("Enabled if hardware matches");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_NONE) {
		/* TRANSLATORS: Plugin is active and in use */
		return _("Enabled");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_DISABLED) {
		/* TRANSLATORS: Plugin is inactive and not used */
		return _("Disabled");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_NO_HARDWARE) {
		/* TRANSLATORS: not required for this system */
		return _("Required hardware was not found");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_LEGACY_BIOS) {
		/* TRANSLATORS: system is not booted in UEFI mode */
		return _("UEFI firmware can not be updated in legacy BIOS mode");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED) {
		/* TRANSLATORS: capsule updates are an optional BIOS feature */
		return _("UEFI capsule updates not available or enabled in firmware setup");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED) {
		/* TRANSLATORS: user needs to run a command */
		return _("Firmware updates disabled; run 'fwupdmgr unlock' to enable");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_AUTH_REQUIRED) {
		/* TRANSLATORS: user needs to run a command */
		return _("Authentication details are required");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED) {
		/* TRANSLATORS: the user is using Gentoo/Arch and has screwed something up */
		return _("Required efivarfs filesystem was not found");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND) {
		/* TRANSLATORS: partition refers to something on disk, again, hey Arch users */
		return _("UEFI ESP partition not detected or configured");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_FAILED_OPEN) {
		/* TRANSLATORS: Failed to open plugin, hey Arch users */
		return _("Plugin dependencies missing");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_KERNEL_TOO_OLD) {
		/* TRANSLATORS: The kernel does not support this plugin */
		return _("Running kernel is too old");
	}

	/* fall back for unknown types */
	return fwupd_plugin_flag_to_string(plugin_flag);
}

static gchar *
fu_util_plugin_flag_to_cli_text(FwupdPluginFlags plugin_flag)
{
	switch (plugin_flag) {
	case FWUPD_PLUGIN_FLAG_UNKNOWN:
	case FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE:
	case FWUPD_PLUGIN_FLAG_USER_WARNING:
		return NULL;
	case FWUPD_PLUGIN_FLAG_NONE:
	case FWUPD_PLUGIN_FLAG_REQUIRE_HWID:
		return fu_util_term_format(fu_util_plugin_flag_to_string(plugin_flag),
					   FU_UTIL_TERM_COLOR_GREEN);
	case FWUPD_PLUGIN_FLAG_DISABLED:
	case FWUPD_PLUGIN_FLAG_NO_HARDWARE:
		return fu_util_term_format(fu_util_plugin_flag_to_string(plugin_flag),
					   FU_UTIL_TERM_COLOR_BLACK);
	case FWUPD_PLUGIN_FLAG_LEGACY_BIOS:
	case FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED:
	case FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED:
	case FWUPD_PLUGIN_FLAG_AUTH_REQUIRED:
	case FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED:
	case FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND:
	case FWUPD_PLUGIN_FLAG_KERNEL_TOO_OLD:
	default:
		break;
	}

	/* fall back for unknown types */
	return g_strdup(fwupd_plugin_flag_to_string(plugin_flag));
}

gchar *
fu_util_plugin_to_string(FwupdPlugin *plugin, guint idt)
{
	GString *str = g_string_new(NULL);
	const gchar *hdr;
	guint64 flags = fwupd_plugin_get_flags(plugin);

	fu_common_string_append_kv(str, idt, fwupd_plugin_get_name(plugin), NULL);

	/* TRANSLATORS: description of plugin state, e.g. disabled */
	hdr = _("Flags");
	if (flags == 0x0) {
		const gchar *tmp = fu_util_plugin_flag_to_cli_text(flags);
		g_autofree gchar *li = g_strdup_printf("• %s", tmp);
		fu_common_string_append_kv(str, idt + 1, hdr, li);
	} else {
		for (guint i = 0; i < 64; i++) {
			g_autofree gchar *li = NULL;
			g_autofree gchar *tmp = NULL;
			if ((flags & ((guint64)1 << i)) == 0)
				continue;
			tmp = fu_util_plugin_flag_to_cli_text((guint64)1 << i);
			if (tmp == NULL)
				continue;
			li = g_strdup_printf("• %s", tmp);
			fu_common_string_append_kv(str, idt + 1, hdr, li);

			/* clear header */
			hdr = "";
		}
	}

	return g_string_free(str, FALSE);
}

static const gchar *
fu_util_license_to_string(const gchar *license)
{
	if (license == NULL) {
		/* TRANSLATORS: we don't know the license of the update */
		return _("Unknown");
	}
	if (g_strcmp0(license, "LicenseRef-proprietary") == 0 ||
	    g_strcmp0(license, "proprietary") == 0) {
		/* TRANSLATORS: a non-free software license */
		return _("Proprietary");
	}
	return license;
}

static const gchar *
fu_util_release_urgency_to_string(FwupdReleaseUrgency release_urgency)
{
	if (release_urgency == FWUPD_RELEASE_URGENCY_LOW) {
		/* TRANSLATORS: the release urgency */
		return _("Low");
	}
	if (release_urgency == FWUPD_RELEASE_URGENCY_MEDIUM) {
		/* TRANSLATORS: the release urgency */
		return _("Medium");
	}
	if (release_urgency == FWUPD_RELEASE_URGENCY_HIGH) {
		/* TRANSLATORS: the release urgency */
		return _("High");
	}
	if (release_urgency == FWUPD_RELEASE_URGENCY_CRITICAL) {
		/* TRANSLATORS: the release urgency */
		return _("Critical");
	}
	/* TRANSLATORS: unknown release urgency */
	return _("Unknown");
}

static const gchar *
fu_util_release_flag_to_string(FwupdReleaseFlags release_flag)
{
	if (release_flag == FWUPD_RELEASE_FLAG_NONE)
		return NULL;
	if (release_flag == FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD) {
		/* TRANSLATORS: We verified the payload against the server */
		return _("Trusted payload");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_TRUSTED_METADATA) {
		/* TRANSLATORS: We verified the meatdata against the server */
		return _("Trusted metadata");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_IS_UPGRADE) {
		/* TRANSLATORS: version is newer */
		return _("Is upgrade");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_IS_DOWNGRADE) {
		/* TRANSLATORS: version is older */
		return _("Is downgrade");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_BLOCKED_VERSION) {
		/* TRANSLATORS: version cannot be installed due to policy */
		return _("Blocked version");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL) {
		/* TRANSLATORS: version cannot be installed due to policy */
		return _("Not approved");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH) {
		/* TRANSLATORS: is not the main firmware stream */
		return _("Alternate branch");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_IS_COMMUNITY) {
		/* TRANSLATORS: is not supported by the vendor */
		return _("Community supported");
	}

	/* fall back for unknown types */
	return fwupd_release_flag_to_string(release_flag);
}

gchar *
fu_util_release_to_string(FwupdRelease *rel, guint idt)
{
	const gchar *title;
	const gchar *tmp2;
	GPtrArray *issues = fwupd_release_get_issues(rel);
	GPtrArray *tags = fwupd_release_get_tags(rel);
	GString *str = g_string_new(NULL);
	guint64 flags = fwupd_release_get_flags(rel);
	g_autofree gchar *desc_fb = NULL;

	g_return_val_if_fail(FWUPD_IS_RELEASE(rel), NULL);

	fu_common_string_append_kv(str, idt, fwupd_release_get_name(rel), NULL);

	/* TRANSLATORS: version number of new firmware */
	fu_common_string_append_kv(str, idt + 1, _("New version"), fwupd_release_get_version(rel));

	if (fwupd_release_get_remote_id(rel) != NULL) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: the server the file is coming from */
					   _("Remote ID"),
					   fwupd_release_get_remote_id(rel));
	}
	if (fwupd_release_get_id(rel) != NULL) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: the exact component on the server */
					   _("Release ID"),
					   fwupd_release_get_id(rel));
	}
	if (fwupd_release_get_branch(rel) != NULL) {
		fu_common_string_append_kv(
		    str,
		    idt + 1,
		    /* TRANSLATORS: the stream of firmware, e.g. nonfree or open-source */
		    _("Branch"),
		    fwupd_release_get_branch(rel));
	}
	if (fwupd_release_get_summary(rel) != NULL) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: one line summary of device */
					   _("Summary"),
					   fwupd_release_get_summary(rel));
	}
	if (fwupd_release_get_name_variant_suffix(rel) != NULL) {
		fu_common_string_append_kv(
		    str,
		    idt + 1,
		    /* TRANSLATORS: one line variant of release (e.g. 'Prerelease' or 'China') */
		    _("Variant"),
		    fwupd_release_get_name_variant_suffix(rel));
	}
	fu_common_string_append_kv(str,
				   idt + 1,
				   /* TRANSLATORS: e.g. GPLv2+, Proprietary etc */
				   _("License"),
				   fu_util_license_to_string(fwupd_release_get_license(rel)));
	if (fwupd_release_get_size(rel) != 0) {
		g_autofree gchar *tmp = NULL;
		tmp = g_format_size(fwupd_release_get_size(rel));
		/* TRANSLATORS: file size of the download */
		fu_common_string_append_kv(str, idt + 1, _("Size"), tmp);
	}
	if (fwupd_release_get_created(rel) != 0) {
		gint64 value = (gint64)fwupd_release_get_created(rel);
		g_autoptr(GDateTime) date = g_date_time_new_from_unix_utc(value);
		g_autofree gchar *tmp = g_date_time_format(date, "%F");
		/* TRANSLATORS: when the update was built */
		fu_common_string_append_kv(str, idt + 1, _("Created"), tmp);
	}
	if (fwupd_release_get_urgency(rel) != FWUPD_RELEASE_URGENCY_UNKNOWN) {
		FwupdReleaseUrgency tmp = fwupd_release_get_urgency(rel);
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: how important the release is */
					   _("Urgency"),
					   fu_util_release_urgency_to_string(tmp));
	}
	if (fwupd_release_get_details_url(rel) != NULL) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: more details about the update link */
					   _("Details"),
					   fwupd_release_get_details_url(rel));
	}
	if (fwupd_release_get_source_url(rel) != NULL) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: source (as in code) link */
					   _("Source"),
					   fwupd_release_get_source_url(rel));
	}
	if (fwupd_release_get_vendor(rel) != NULL) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: manufacturer of hardware */
					   _("Vendor"),
					   fwupd_release_get_vendor(rel));
	}
	if (fwupd_release_get_install_duration(rel) != 0) {
		g_autofree gchar *tmp =
		    fu_util_time_to_str(fwupd_release_get_install_duration(rel));
		/* TRANSLATORS: length of time the update takes to apply */
		fu_common_string_append_kv(str, idt + 1, _("Duration"), tmp);
	}
	if (fwupd_release_get_update_message(rel) != NULL) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: helpful messages for the update */
					   _("Update Message"),
					   fwupd_release_get_update_message(rel));
	}
	if (fwupd_release_get_update_image(rel) != NULL) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: helpful image for the update */
					   _("Update Image"),
					   fwupd_release_get_update_image(rel));
	}

	/* TRANSLATORS: release attributes */
	title = _("Release Flags");
	for (guint i = 0; i < 64; i++) {
		g_autofree gchar *bullet = NULL;
		if ((flags & ((guint64)1 << i)) == 0)
			continue;
		tmp2 = fu_util_release_flag_to_string((guint64)1 << i);
		if (tmp2 == NULL)
			continue;
		bullet = g_strdup_printf("• %s", tmp2);
		fu_common_string_append_kv(str, idt + 1, title, bullet);
		title = "";
	}

	desc_fb = fu_util_get_release_description_with_fallback(rel);
	if (desc_fb != NULL) {
		g_autofree gchar *desc = NULL;
		desc = fu_util_convert_description(desc_fb, NULL);
		if (desc == NULL)
			desc = g_strdup(fwupd_release_get_description(rel));
		/* TRANSLATORS: multiline description of device */
		fu_common_string_append_kv(str, idt + 1, _("Description"), desc);
	}
	for (guint i = 0; i < issues->len; i++) {
		const gchar *issue = g_ptr_array_index(issues, i);
		if (i == 0) {
			fu_common_string_append_kv(
			    str,
			    idt + 1,
			    /* TRANSLATORS: issue fixed with the release, e.g. CVE */
			    ngettext("Issue", "Issues", issues->len),
			    issue);
		} else {
			fu_common_string_append_kv(str, idt + 1, "", issue);
		}
	}
	if (tags->len > 0) {
		g_autofree gchar *tag_strs = fu_common_strjoin_array(", ", tags);
		fu_common_string_append_kv(
		    str,
		    idt + 1,
		    /* TRANSLATORS: release tag set for release, e.g. lenovo-2021q3 */
		    ngettext("Tag", "Tags", tags->len),
		    tag_strs);
	}

	return g_string_free(str, FALSE);
}

gchar *
fu_util_remote_to_string(FwupdRemote *remote, guint idt)
{
	GString *str = g_string_new(NULL);
	FwupdRemoteKind kind = fwupd_remote_get_kind(remote);
	FwupdKeyringKind keyring_kind = fwupd_remote_get_keyring_kind(remote);
	const gchar *tmp;
	gint priority;

	g_return_val_if_fail(FWUPD_IS_REMOTE(remote), NULL);

	fu_common_string_append_kv(str, idt, fwupd_remote_get_title(remote), NULL);

	/* TRANSLATORS: remote identifier, e.g. lvfs-testing */
	fu_common_string_append_kv(str, idt + 1, _("Remote ID"), fwupd_remote_get_id(remote));

	/* TRANSLATORS: remote type, e.g. remote or local */
	fu_common_string_append_kv(str, idt + 1, _("Type"), fwupd_remote_kind_to_string(kind));

	if (keyring_kind != FWUPD_KEYRING_KIND_UNKNOWN) {
		fu_common_string_append_kv(str,
					   idt + 1,
					   /* TRANSLATORS: keyring type, e.g. GPG or PKCS7 */
					   _("Keyring"),
					   fwupd_keyring_kind_to_string(keyring_kind));
	}

	fu_common_string_append_kv(str,
				   idt + 1,
				   /* TRANSLATORS: if the remote is enabled */
				   _("Enabled"),
				   fwupd_remote_get_enabled(remote) ? "true" : "false");

	tmp = fwupd_remote_get_checksum(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote checksum */
		fu_common_string_append_kv(str, idt + 1, _("Checksum"), tmp);
	}

	/* optional parameters */
	if (kind == FWUPD_REMOTE_KIND_DOWNLOAD && fwupd_remote_get_age(remote) > 0 &&
	    fwupd_remote_get_age(remote) != G_MAXUINT64) {
		const gchar *unit = "s";
		gdouble age = fwupd_remote_get_age(remote);
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
		age_str = g_strdup_printf("%.2f%s", age, unit);
		/* TRANSLATORS: the age of the metadata */
		fu_common_string_append_kv(str, idt + 1, _("Age"), age_str);
	}
	priority = fwupd_remote_get_priority(remote);
	if (priority != 0) {
		g_autofree gchar *priority_str = NULL;
		priority_str = g_strdup_printf("%i", priority);
		/* TRANSLATORS: the numeric priority */
		fu_common_string_append_kv(str, idt + 1, _("Priority"), priority_str);
	}
	tmp = fwupd_remote_get_username(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote filename base */
		fu_common_string_append_kv(str, idt + 1, _("Username"), tmp);
	}
	tmp = fwupd_remote_get_password(remote);
	if (tmp != NULL) {
		g_autofree gchar *hidden = g_strnfill(fu_common_strwidth(tmp), '*');
		/* TRANSLATORS: remote filename base */
		fu_common_string_append_kv(str, idt + 1, _("Password"), hidden);
	}
	tmp = fwupd_remote_get_filename_cache(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: filename of the local file */
		fu_common_string_append_kv(str, idt + 1, _("Filename"), tmp);
	}
	tmp = fwupd_remote_get_filename_cache_sig(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: filename of the local file */
		fu_common_string_append_kv(str, idt + 1, _("Filename Signature"), tmp);
	}
	tmp = fwupd_remote_get_filename_source(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: full path of the remote.conf file */
		fu_common_string_append_kv(str, idt + 1, _("Filename Source"), tmp);
	}
	tmp = fwupd_remote_get_metadata_uri(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote URI */
		fu_common_string_append_kv(str, idt + 1, _("Metadata URI"), tmp);
	}
	tmp = fwupd_remote_get_metadata_uri_sig(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote URI */
		fu_common_string_append_kv(str, idt + 1, _("Metadata Signature"), tmp);
	}
	tmp = fwupd_remote_get_firmware_base_uri(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: remote URI */
		fu_common_string_append_kv(str, idt + 1, _("Firmware Base URI"), tmp);
	}
	tmp = fwupd_remote_get_report_uri(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: URI to send success/failure reports */
		fu_common_string_append_kv(str, idt + 1, _("Report URI"), tmp);
		fu_common_string_append_kv(
		    str,
		    idt + 1,
		    /* TRANSLATORS: Boolean value to automatically send reports */
		    _("Automatic Reporting"),
		    fwupd_remote_get_automatic_reports(remote) ? "true" : "false");
	}

	return g_string_free(str, FALSE);
}

static void
fu_security_attr_append_str(FwupdSecurityAttr *attr,
			    GString *str,
			    FuSecurityAttrToStringFlags flags)
{
	g_autofree gchar *name = NULL;

	/* hide obsoletes by default */
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED) &&
	    (flags & FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_OBSOLETES) == 0)
		return;

	name = fu_security_attr_get_name(attr);
	if (name == NULL)
		name = g_strdup(fwupd_security_attr_get_appstream_id(attr));
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)) {
		g_string_append(str, "✦ ");
	} else if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_string_append(str, "✔ ");
	} else {
		g_string_append(str, "✘ ");
	}
	g_string_append_printf(str, "%s:", name);
	for (guint i = fu_common_strwidth(name); i < 30; i++)
		g_string_append(str, " ");
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)) {
		g_string_append_printf(str,
				       "\033[37m\033[1m%s\033[0m",
				       fu_security_attr_get_result(attr));
	} else if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_string_append_printf(str,
				       "\033[32m\033[1m%s\033[0m",
				       fu_security_attr_get_result(attr));
	} else {
		g_string_append_printf(str,
				       "\033[31m\033[1m%s\033[0m",
				       fu_security_attr_get_result(attr));
	}
	if ((flags & FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_URLS) > 0 &&
	    fwupd_security_attr_get_url(attr) != NULL) {
		g_string_append_printf(str, ": %s", fwupd_security_attr_get_url(attr));
	}
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)) {
		/* TRANSLATORS: this is shown as a suffix for obsoleted tests */
		g_string_append_printf(str, " %s", _("(obsoleted)"));
	}
	g_string_append_printf(str, "\n");
}

static gchar *
fu_util_security_event_to_string(FwupdSecurityAttr *attr)
{
	struct {
		const gchar *appstream_id;
		FwupdSecurityAttrResult result_old;
		FwupdSecurityAttrResult result_new;
		const gchar *text;
	} items[] = {{FWUPD_SECURITY_ATTR_ID_IOMMU,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("IOMMU device protection enabled")},
		     {FWUPD_SECURITY_ATTR_ID_IOMMU,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND,
		      /* TRANSLATORS: HSI event title */
		      _("IOMMU device protection disabled")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS,
		      FWUPD_SECURITY_ATTR_RESULT_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED,
		      NULL},
		     {FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_TAINTED,
		      NULL},
		     {FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS,
		      FWUPD_SECURITY_ATTR_RESULT_UNKNOWN,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      NULL},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED,
		      /* TRANSLATORS: HSI event title */
		      _("Kernel is tainted")},
		     {FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_TAINTED,
		      /* TRANSLATORS: HSI event title */
		      _("Kernel is no longer tainted")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Kernel lockdown disabled")},
		     {FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Kernel lockdown enabled")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_ACPI_DMAR,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Pre-boot DMA protection is disabled")},
		     {FWUPD_SECURITY_ATTR_ID_ACPI_DMAR,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Pre-boot DMA protection is enabled")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Secure Boot disabled")},
		     {FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Secure Boot enabled")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
		      FWUPD_SECURITY_ATTR_RESULT_UNKNOWN,
		      FWUPD_SECURITY_ATTR_RESULT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("All TPM PCRs are valid")},
		     {FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
		      FWUPD_SECURITY_ATTR_RESULT_VALID,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("A TPM PCR is now an invalid value")},
		     {FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
		      FWUPD_SECURITY_ATTR_RESULT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("All TPM PCRs are now valid")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("TPM PCR0 reconstruction is invalid")},
		     {NULL, 0, 0, NULL}};

	/* sanity check */
	if (fwupd_security_attr_get_appstream_id(attr) == NULL)
		return NULL;
	if (fwupd_security_attr_get_result(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN &&
	    fwupd_security_attr_get_result_fallback(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN)
		return NULL;

	/* look for prepared text */
	for (guint i = 0; items[i].appstream_id != NULL; i++) {
		if (g_strcmp0(fwupd_security_attr_get_appstream_id(attr), items[i].appstream_id) ==
			0 &&
		    fwupd_security_attr_get_result(attr) == items[i].result_new &&
		    fwupd_security_attr_get_result_fallback(attr) == items[i].result_old)
			return g_strdup(items[i].text);
	}

	/* disappeared */
	if (fwupd_security_attr_get_result(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN) {
		return g_strdup_printf(
		    /* TRANSLATORS: %1 refers to some kind of security test, e.g. "SPI BIOS region".
		       %2 refers to a result value, e.g. "Invalid" */
		    _("%s disappeared: %s"),
		    fu_security_attr_get_name(attr),
		    fu_security_attr_result_to_string(
			fwupd_security_attr_get_result_fallback(attr)));
	}

	/* appeared */
	if (fwupd_security_attr_get_result_fallback(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN) {
		return g_strdup_printf(
		    /* TRANSLATORS: %1 refers to some kind of security test, e.g. "Encrypted RAM".
		       %2 refers to a result value, e.g. "Invalid" */
		    _("%s appeared: %s"),
		    fu_security_attr_get_name(attr),
		    fu_security_attr_result_to_string(fwupd_security_attr_get_result(attr)));
	}

	/* fall back to something sensible */
	return g_strdup_printf(
	    /* TRANSLATORS: %1 refers to some kind of security test, e.g. "UEFI platform key".
	     * %2 and %3 refer to results value, e.g. "Valid" and "Invalid" */
	    _("%s changed: %s → %s"),
	    fu_security_attr_get_name(attr),
	    fu_security_attr_result_to_string(fwupd_security_attr_get_result_fallback(attr)),
	    fu_security_attr_result_to_string(fwupd_security_attr_get_result(attr)));
}

gchar *
fu_util_security_events_to_string(GPtrArray *events, FuSecurityAttrToStringFlags strflags)
{
	g_autoptr(GString) str = g_string_new(NULL);

	/* debugging */
	if (g_getenv("FWUPD_VERBOSE") != NULL) {
		for (guint i = 0; i < events->len; i++) {
			FwupdSecurityAttr *attr = g_ptr_array_index(events, i);
			g_autofree gchar *tmp = fwupd_security_attr_to_string(attr);
			g_debug("%s", tmp);
		}
	}

	for (guint i = 0; i < events->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(events, i);
		g_autoptr(GDateTime) date = NULL;
		g_autofree gchar *dtstr = NULL;
		g_autofree gchar *check = NULL;
		g_autofree gchar *eventstr = NULL;

		date = g_date_time_new_from_unix_utc((gint64)fwupd_security_attr_get_created(attr));
		dtstr = g_date_time_format(date, "%F %T");
		eventstr = fu_util_security_event_to_string(attr);
		if (eventstr == NULL)
			continue;
		if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
			check = fu_util_term_format("✔", FU_UTIL_TERM_COLOR_GREEN);
		} else {
			check = fu_util_term_format("✘", FU_UTIL_TERM_COLOR_RED);
		}
		if (str->len == 0) {
			/* TRANSLATORS: title for host security events */
			g_string_append_printf(str, "%s\n", _("Host Security Events"));
		}
		g_string_append_printf(str, "  %s:  %s %s\n", dtstr, check, eventstr);
	}

	/* no output required */
	if (str->len == 0)
		return NULL;

	/* success */
	return g_string_free(g_steal_pointer(&str), FALSE);
}

gchar *
fu_util_security_issues_to_string(GPtrArray *devices)
{
	g_autoptr(GString) str = g_string_new(NULL);

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index(devices, i);
		GPtrArray *issues = fwupd_device_get_issues(device);
		if (issues->len == 0)
			continue;
		if (str->len == 0) {
			g_string_append_printf(
			    str,
			    "%s\n",
			    /* TRANSLATORS: now list devices with unfixed high-priority issues */
			    _("There are devices with issues:"));
		}
		g_string_append_printf(str,
				       "\n  %s — %s:\n",
				       fwupd_device_get_vendor(device),
				       fwupd_device_get_name(device));
		for (guint j = 0; j < issues->len; j++) {
			const gchar *issue = g_ptr_array_index(issues, j);
			g_string_append_printf(str, "   • %s\n", issue);
		}
	}

	/* no output required */
	if (str->len == 0)
		return NULL;

	/* success */
	return g_string_free(g_steal_pointer(&str), FALSE);
}

gchar *
fu_util_security_attrs_to_string(GPtrArray *attrs, FuSecurityAttrToStringFlags strflags)
{
	FwupdSecurityAttrFlags flags = FWUPD_SECURITY_ATTR_FLAG_NONE;
	const FwupdSecurityAttrFlags hpi_suffixes[] = {
	    FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE,
	    FWUPD_SECURITY_ATTR_FLAG_NONE,
	};
	GString *str = g_string_new(NULL);
	gboolean low_help = FALSE;
	gboolean runtime_help = FALSE;
	gboolean pcr0_help = FALSE;

	for (guint j = 1; j <= FWUPD_SECURITY_ATTR_LEVEL_LAST; j++) {
		gboolean has_header = FALSE;
		for (guint i = 0; i < attrs->len; i++) {
			FwupdSecurityAttr *attr = g_ptr_array_index(attrs, i);
			if (fwupd_security_attr_get_level(attr) != j)
				continue;
			if (!has_header) {
				g_string_append_printf(str, "\n\033[1mHSI-%u\033[0m\n", j);
				has_header = TRUE;
			}
			fu_security_attr_append_str(attr, str, strflags);
			/* make sure they have at least HSI-1 */
			if (j < FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT &&
			    !fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
				low_help = TRUE;

			/* check for PCR0 not matching */
			if (g_strcmp0(fwupd_security_attr_get_appstream_id(attr),
				      FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0) == 0 &&
			    fwupd_security_attr_get_result(attr) ==
				FWUPD_SECURITY_ATTR_RESULT_NOT_VALID)
				pcr0_help = TRUE;
		}
	}
	for (guint i = 0; i < attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(attrs, i);
		flags |= fwupd_security_attr_get_flags(attr);
	}
	for (guint j = 0; hpi_suffixes[j] != FWUPD_SECURITY_ATTR_FLAG_NONE; j++) {
		if (flags & hpi_suffixes[j]) {
			g_string_append_printf(str,
					       "\n\033[1m%s -%s\033[0m\n",
					       /* TRANSLATORS:  this is the HSI suffix */
					       _("Runtime Suffix"),
					       fwupd_security_attr_flag_to_suffix(hpi_suffixes[j]));
			for (guint i = 0; i < attrs->len; i++) {
				FwupdSecurityAttr *attr = g_ptr_array_index(attrs, i);
				if (!fwupd_security_attr_has_flag(attr, hpi_suffixes[j]))
					continue;
				if (fwupd_security_attr_has_flag(
					attr,
					FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE) &&
				    !fwupd_security_attr_has_flag(attr,
								  FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
					runtime_help = TRUE;
				fu_security_attr_append_str(attr, str, strflags);
			}
		}
	}

	if (low_help) {
		g_string_append_printf(
		    str,
		    "\n%s\n » %s\n",
		    /* TRANSLATORS: this is instructions on how to improve the HSI security level */
		    _("This system has a low HSI security level."),
		    "https://github.com/fwupd/fwupd/wiki/Low-host-security-level");
	}
	if (runtime_help) {
		g_string_append_printf(
		    str,
		    "\n%s\n » %s\n",
		    /* TRANSLATORS: this is instructions on how to improve the HSI suffix */
		    _("This system has HSI runtime issues."),
		    "https://github.com/fwupd/fwupd/wiki/Host-security-ID-runtime-issues");
	}

	if (pcr0_help) {
		g_string_append_printf(
		    str,
		    "\n%s\n » %s\n",
		    /* TRANSLATORS: this is more background on a security measurement problem */
		    _("The TPM PCR0 differs from reconstruction."),
		    "https://github.com/fwupd/fwupd/wiki/TPM-PCR0-differs-from-reconstruction");
	}

	return g_string_free(str, FALSE);
}

gboolean
fu_util_send_report(FwupdClient *client,
		    const gchar *report_uri,
		    const gchar *data,
		    const gchar *sig,
		    gchar **uri, /* (nullable) (out) */
		    GError **error)
{
	const gchar *server_msg = NULL;
	JsonNode *json_root;
	JsonObject *json_object;
	g_autofree gchar *str = NULL;
	g_autoptr(GBytes) upload_response = NULL;
	g_autoptr(JsonParser) json_parser = NULL;

	/* POST request */
	upload_response = fwupd_client_upload_bytes(client,
						    report_uri,
						    data,
						    sig,
						    FWUPD_CLIENT_UPLOAD_FLAG_NONE,
						    NULL,
						    error);
	if (upload_response == NULL)
		return FALSE;

	/* server returned nothing, and probably exploded in a ball of flames */
	if (g_bytes_get_size(upload_response) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Failed to upload to %s",
			    report_uri);
		return FALSE;
	}

	/* parse JSON reply */
	json_parser = json_parser_new();
	str = g_strndup(g_bytes_get_data(upload_response, NULL), g_bytes_get_size(upload_response));
	if (!json_parser_load_from_data(json_parser, str, -1, error)) {
		g_prefix_error(error, "Failed to parse JSON response from '%s': ", str);
		return FALSE;
	}
	json_root = json_parser_get_root(json_parser);
	if (json_root == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_PERMISSION_DENIED,
			    "JSON response was malformed: '%s'",
			    str);
		return FALSE;
	}
	json_object = json_node_get_object(json_root);
	if (json_object == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_PERMISSION_DENIED,
			    "JSON response object was malformed: '%s'",
			    str);
		return FALSE;
	}

	/* get any optional server message */
	if (json_object_has_member(json_object, "msg"))
		server_msg = json_object_get_string_member(json_object, "msg");

	/* server reported failed */
	if (!json_object_get_boolean_member(json_object, "success")) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_PERMISSION_DENIED,
			    "Server rejected report: %s",
			    server_msg != NULL ? server_msg : "unspecified");
		return FALSE;
	}

	/* server wanted us to see the message */
	if (server_msg != NULL) {
		g_debug("server message: %s", server_msg);
		if (g_strstr_len(server_msg, -1, "known issue") != NULL &&
		    json_object_has_member(json_object, "uri")) {
			if (uri != NULL)
				*uri = g_strdup(json_object_get_string_member(json_object, "uri"));
		}
	}

	/* success */
	return TRUE;
}

gint
fu_util_sort_devices_by_flags_cb(gconstpointer a, gconstpointer b)
{
	FuDevice *dev_a = *((FuDevice **)a);
	FuDevice *dev_b = *((FuDevice **)b);

	if ((!fu_device_has_flag(dev_a, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	     fu_device_has_flag(dev_b, FWUPD_DEVICE_FLAG_UPDATABLE)) ||
	    (!fu_device_has_flag(dev_a, FWUPD_DEVICE_FLAG_SUPPORTED) &&
	     fu_device_has_flag(dev_b, FWUPD_DEVICE_FLAG_SUPPORTED)))
		return -1;
	if ((fu_device_has_flag(dev_a, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	     !fu_device_has_flag(dev_b, FWUPD_DEVICE_FLAG_UPDATABLE)) ||
	    (fu_device_has_flag(dev_a, FWUPD_DEVICE_FLAG_SUPPORTED) &&
	     !fu_device_has_flag(dev_b, FWUPD_DEVICE_FLAG_SUPPORTED)))
		return 1;

	return 0;
}

static gint
fu_util_device_order_compare(FuDevice *device1, FuDevice *device2)
{
	if (fu_device_get_order(device1) < fu_device_get_order(device2))
		return -1;
	if (fu_device_get_order(device1) > fu_device_get_order(device2))
		return 1;
	return 0;
}

gint
fu_util_device_order_sort_cb(gconstpointer a, gconstpointer b)
{
	FuDevice *device_a = *((FuDevice **)a);
	FuDevice *device_b = *((FuDevice **)b);
	return fu_util_device_order_compare(device_a, device_b);
}

gboolean
fu_util_switch_branch_warning(FwupdDevice *dev,
			      FwupdRelease *rel,
			      gboolean assume_yes,
			      GError **error)
{
	const gchar *desc_markup = NULL;
	g_autofree gchar *desc_plain = NULL;
	g_autofree gchar *title = NULL;
	g_autoptr(GString) desc_full = g_string_new(NULL);

	/* warn the user if the vendor is different */
	if (g_strcmp0(fwupd_device_get_vendor(dev), fwupd_release_get_vendor(rel)) != 0) {
		g_string_append_printf(
		    desc_full,
		    /* TRANSLATORS: %1 is the firmware vendor, %2 is the device vendor name */
		    _("The firmware from %s is not "
		      "supplied by %s, the hardware vendor."),
		    fwupd_release_get_vendor(rel),
		    fwupd_device_get_vendor(dev));
		g_string_append(desc_full, "\n\n");
		g_string_append_printf(desc_full,
				       /* TRANSLATORS: %1 is the device vendor name */
				       _("Your hardware may be damaged using this firmware, "
					 "and installing this release may void any warranty "
					 "with %s."),
				       fwupd_device_get_vendor(dev));
		g_string_append(desc_full, "\n\n");
	}

	/* from the <description> in the AppStream data */
	desc_markup = fwupd_release_get_description(rel);
	if (desc_markup == NULL)
		return TRUE;
	desc_plain = fu_util_convert_description(desc_markup, error);
	if (desc_plain == NULL)
		return FALSE;
	g_string_append(desc_full, desc_plain);

	/* TRANSLATORS: show and ask user to confirm --
	 * %1 is the old branch name, %2 is the new branch name */
	title = g_strdup_printf(_("Switch branch from %s to %s?"),
				fu_util_branch_for_display(fwupd_device_get_branch(dev)),
				fu_util_branch_for_display(fwupd_release_get_branch(rel)));
	fu_util_warning_box(title, desc_full->str, 80);
	if (!assume_yes) {
		/* ask for permission */
		g_print("\n%s [y|N]: ",
			/* TRANSLATORS: should the branch be changed */
			_("Do you understand the consequences of changing the firmware branch?"));
		if (!fu_util_prompt_for_boolean(FALSE)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "Declined branch switch");
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
fu_util_prompt_warning_fde(FwupdDevice *dev, GError **error)
{
	const gchar *url = "https://github.com/fwupd/fwupd/wiki/Full-Disk-Encryption-Detected";
	g_autoptr(GString) str = g_string_new(NULL);

	if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_AFFECTS_FDE))
		return TRUE;

	g_string_append(
	    str,
	    /* TRANSLATORS: the platform secret is stored in the PCRx registers on the TPM */
	    _("Some of the platform secrets may be invalidated when updating this firmware."));
	g_string_append(str, " ");
	g_string_append(str,
			/* TRANSLATORS: 'recovery key' here refers to a code, rather than a physical
			   metal thing */
			_("Please ensure you have the volume recovery key before continuing."));
	g_string_append(str, "\n\n");
	g_string_append_printf(str,
			       /* TRANSLATORS: the %1 is a URL to a wiki page */
			       _("See %s for more details."),
			       url);
	/* TRANSLATORS: title text, shown as a warning */
	fu_util_warning_box(_("Full Disk Encryption Detected"), str->str, 80);

	/* ask for confirmation */
	g_print("\n%s [Y|n]: ",
		/* TRANSLATORS: prompt to apply the update */
		_("Perform operation?"));
	if (!fu_util_prompt_for_boolean(TRUE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return FALSE;
	}
	return TRUE;
}

void
fu_util_show_unsupported_warn(void)
{
#ifndef SUPPORTED_BUILD
	g_autofree gchar *fmt = NULL;

	if (g_getenv("FWUPD_SUPPORTED") != NULL)
		return;
	/* TRANSLATORS: this is a prefix on the console */
	fmt = fu_util_term_format(_("WARNING:"), FU_UTIL_TERM_COLOR_YELLOW);
	g_printerr("%s %s\n",
		   fmt,
		   /* TRANSLATORS: unsupported build of the package */
		   _("This package has not been validated, it may not work properly."));
#endif
}

#ifdef HAVE_LIBCURL_7_62_0
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURLU, curl_url_cleanup)
#endif

gboolean
fu_util_is_url(const gchar *perhaps_url)
{
#ifdef HAVE_LIBCURL_7_62_0
	g_autoptr(CURLU) h = curl_url();
	return curl_url_set(h, CURLUPART_URL, perhaps_url, 0) == CURLUE_OK;
#else
	return g_str_has_prefix(perhaps_url, "http://") ||
	       g_str_has_prefix(perhaps_url, "https://");
#endif
}

gboolean
fu_util_setup_interactive_console(GError **error)
{
#ifdef _WIN32
	HANDLE hOut;
	DWORD dwMode = 0;

	/* enable VT sequences */
	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to get stdout [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	if (!GetConsoleMode(hOut, &dwMode)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to get mode [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(hOut, dwMode)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to set mode [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	if (!SetConsoleOutputCP(CP_UTF8)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to set output UTF-8 [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	if (!SetConsoleCP(CP_UTF8)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to set UTF-8 [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
#else
	if (isatty(fileno(stdout)) == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "not a TTY");
		return FALSE;
	}
#endif
	/* success */
	return TRUE;
}

gboolean
fu_util_print_builder(JsonBuilder *builder, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* export as a string */
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Failed to convert to JSON string");
		return FALSE;
	}

	/* just print */
	g_print("%s\n", data);
	return TRUE;
}

typedef enum {
	FU_UTIL_DEPENDENCY_KIND_UNKNOWN,
	FU_UTIL_DEPENDENCY_KIND_RUNTIME,
	FU_UTIL_DEPENDENCY_KIND_COMPILE,
} FuUtilDependencyKind;

static const gchar *
fu_util_dependency_kind_to_string(FuUtilDependencyKind dependency_kind)
{
	if (dependency_kind == FU_UTIL_DEPENDENCY_KIND_RUNTIME)
		return "runtime";
	if (dependency_kind == FU_UTIL_DEPENDENCY_KIND_COMPILE)
		return "compile";
	return NULL;
}

static gchar *
fu_util_parse_project_dependency(const gchar *str, FuUtilDependencyKind *dependency_kind)
{
	g_return_val_if_fail(str != NULL, NULL);
	if (g_str_has_prefix(str, "RuntimeVersion(")) {
		gsize strsz = strlen(str);
		if (dependency_kind != NULL)
			*dependency_kind = FU_UTIL_DEPENDENCY_KIND_RUNTIME;
		return g_strndup(str + 15, strsz - 16);
	}
	if (g_str_has_prefix(str, "CompileVersion(")) {
		gsize strsz = strlen(str);
		if (dependency_kind != NULL)
			*dependency_kind = FU_UTIL_DEPENDENCY_KIND_COMPILE;
		return g_strndup(str + 15, strsz - 16);
	}
	return g_strdup(str);
}

static gboolean
fu_util_print_version_key_valid(const gchar *key)
{
	g_return_val_if_fail(key != NULL, FALSE);
	if (g_str_has_prefix(key, "RuntimeVersion"))
		return TRUE;
	if (g_str_has_prefix(key, "CompileVersion"))
		return TRUE;
	return FALSE;
}

gboolean
fu_util_project_versions_as_json(GHashTable *metadata, GError **error)
{
	GHashTableIter iter;
	const gchar *key;
	const gchar *value;
	g_autoptr(JsonBuilder) builder = json_builder_new();

	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "Versions");
	json_builder_begin_array(builder);
	g_hash_table_iter_init(&iter, metadata);
	while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value)) {
		FuUtilDependencyKind dependency_kind = FU_UTIL_DEPENDENCY_KIND_UNKNOWN;
		g_autofree gchar *project = NULL;

		/* add version keys */
		if (!fu_util_print_version_key_valid(key))
			continue;
		project = fu_util_parse_project_dependency(key, &dependency_kind);
		json_builder_begin_object(builder);
		if (dependency_kind != FU_UTIL_DEPENDENCY_KIND_UNKNOWN) {
			json_builder_set_member_name(builder, "Type");
			json_builder_add_string_value(
			    builder,
			    fu_util_dependency_kind_to_string(dependency_kind));
		}
		json_builder_set_member_name(builder, "AppstreamId");
		json_builder_add_string_value(builder, project);
		json_builder_set_member_name(builder, "Version");
		json_builder_add_string_value(builder, value);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
	return fu_util_print_builder(builder, error);
}

gchar *
fu_util_project_versions_to_string(GHashTable *metadata)
{
	GHashTableIter iter;
	const gchar *key;
	const gchar *value;
	g_autoptr(GString) str = g_string_new(NULL);

	g_hash_table_iter_init(&iter, metadata);
	while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value)) {
		FuUtilDependencyKind dependency_kind = FU_UTIL_DEPENDENCY_KIND_UNKNOWN;
		g_autofree gchar *project = NULL;

		/* print version keys */
		if (!fu_util_print_version_key_valid(key))
			continue;
		project = fu_util_parse_project_dependency(key, &dependency_kind);
		g_string_append_printf(str,
				       "%-10s%-30s%s\n",
				       fu_util_dependency_kind_to_string(dependency_kind),
				       project,
				       value);
	}
	return g_string_free(g_steal_pointer(&str), FALSE);
}
