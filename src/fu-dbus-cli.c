/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gi18n.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fu-cli-common.h"
#include "fu-dbus-cli.h"
#include "fu-polkit-agent.h"

struct _FuDbusCli {
	FuCli parent_instance;
};

G_DEFINE_TYPE(FuDbusCli, fu_dbus_cli, FU_TYPE_CLI)

static gboolean
fu_dbus_cli_check_polkit_actions(GError **error)
{
#ifdef POLKIT_ACTIONDIR
	g_autofree gchar *filename = NULL;

	if (g_getenv("FWUPD_POLKIT_NOCHECK") != NULL)
		return TRUE;

	filename = g_build_filename(POLKIT_ACTIONDIR, "org.freedesktop.fwupd.policy", NULL);
	if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_AUTH_FAILED,
		    "PolicyKit files are missing, see "
		    "https://github.com/fwupd/fwupd/wiki/PolicyKit-files-are-missing");
		return FALSE;
	}
#endif

	return TRUE;
}

static void
fu_dbus_cli_init(FuDbusCli *self)
{
	fu_cli_add_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_USES_DAEMON);
}

static void
fu_dbus_cli_class_init(FuDbusCliClass *klass)
{
}

int
main(int argc, char *argv[])
{
	g_autoptr(FuDbusCli) self = g_object_new(FU_TYPE_DBUS_CLI, NULL);
	g_autoptr(GOptionContext) option_context = g_option_context_new(NULL);
	g_autofree gchar *cmd_descriptions = NULL;
	g_autoptr(GError) error = NULL;
#ifdef HAVE_POLKIT
	g_autoptr(FuPolkitAgent) polkit_agent = fu_polkit_agent_new();
#endif

#ifdef _WIN32
	/* workaround Windows setting the codepage to 1252 */
	(void)g_setenv("LANG", "C.UTF-8", FALSE);
#endif

	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);
	g_set_prgname(fu_cli_get_prgname(argv[0]));

	/* ensure D-Bus errors are registered */
	(void)fwupd_error_quark();

	/* make sure polkit actions were installed */
	if (!fu_dbus_cli_check_polkit_actions(&error)) {
		fu_cli_print_error(FU_CLI(self), error);
		return EXIT_FAILURE;
	}

	/* add commands */
	fu_cli_cmd_array_add_common(FU_CLI(self));

	/* get a list of the commands */
	cmd_descriptions = fu_cli_cmd_array_to_string(FU_CLI(self));
	g_option_context_set_summary(option_context, cmd_descriptions);
	g_option_context_set_description(
	    option_context,
	    /* TRANSLATORS: CLI description */
	    _("This tool allows an administrator to query and control the "
	      "fwupd daemon, allowing them to perform actions such as "
	      "installing or downgrading firmware."));

	/* TRANSLATORS: program name */
	g_set_application_name(_("Firmware Utility"));
	g_option_context_add_group(option_context, fu_cli_get_option_group(FU_CLI(self)));
	if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
		fu_console_print(fu_cli_get_console(FU_CLI(self)),
				 "%s: %s",
				 /* TRANSLATORS: the user didn't read the man page */
				 _("Failed to parse arguments"),
				 error->message);
		return EXIT_FAILURE;
	}

#ifdef HAVE_POLKIT
	/* start polkit tty agent to listen for password requests */
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_IS_INTERACTIVE)) {
		g_autoptr(GError) error_polkit = NULL;
		g_autoptr(FuPathStore) pstore = fu_path_store_new();
		fu_path_store_load_from_env(pstore);
		if (!fu_polkit_agent_open(polkit_agent, pstore, &error_polkit)) {
			fu_console_print(fu_cli_get_console(FU_CLI(self)),
					 "Failed to open polkit agent: %s",
					 error_polkit->message);
		}
	}
#endif

	/* process command line arguments */
	return fu_cli_main(FU_CLI(self), argc, argv);
}
