/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib-unix.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fu-context-private.h"
#include "fu-ucs2.h"
#include "fu-uefi-backend.h"
#include "fu-uefi-cod-device.h"
#include "fu-uefi-common.h"
#include "fu-uefi-grub-device.h"
#include "fu-uefi-nvram-device.h"
#include "fu-uefi-update-info.h"

/* custom return code */
#define EXIT_NOTHING_TO_DO 2

typedef struct {
	GCancellable *cancellable;
	GMainLoop *loop;
	GOptionContext *context;
} FuUtilPrivate;

static void
fu_util_ignore_cb(const gchar *log_domain,
		  GLogLevelFlags log_level,
		  const gchar *message,
		  gpointer user_data)
{
}

static void
fu_util_private_free(FuUtilPrivate *priv)
{
	if (priv->context != NULL)
		g_option_context_free(priv->context);
	g_free(priv);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtilPrivate, fu_util_private_free)
#pragma clang diagnostic pop

int
main(int argc, char *argv[])
{
	gboolean action_enable = FALSE;
	gboolean action_info = FALSE;
	gboolean action_list = FALSE;
	gboolean action_log = FALSE;
	gboolean action_set_debug = FALSE;
	gboolean action_supported = FALSE;
	gboolean action_unset_debug = FALSE;
	gboolean action_version = FALSE;
	gboolean ret;
	gboolean verbose = FALSE;
	g_autofree gchar *apply = FALSE;
	g_autofree gchar *type = FALSE;
	g_autofree gchar *esp_path = NULL;
	g_autofree gchar *flags = FALSE;
	g_autoptr(FuUtilPrivate) priv = g_new0(FuUtilPrivate, 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(FuVolume) esp = NULL;
	const GOptionEntry options[] = {
	    {"verbose",
	     'v',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_NONE,
	     &verbose,
	     /* TRANSLATORS: command line option */
	     _("Show extra debugging information"),
	     NULL},
	    {"version",
	     '\0',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_NONE,
	     &action_version,
	     /* TRANSLATORS: command line option */
	     _("Display version"),
	     NULL},
	    {"log",
	     'L',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_NONE,
	     &action_log,
	     /* TRANSLATORS: command line option */
	     _("Show the debug log from the last attempted update"),
	     NULL},
	    {"list",
	     'l',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_NONE,
	     &action_list,
	     /* TRANSLATORS: command line option */
	     _("List supported firmware updates"),
	     NULL},
	    {"supported",
	     's',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_NONE,
	     &action_supported,
	     /* TRANSLATORS: command line option */
	     _("Query for firmware update support"),
	     NULL},
	    {"info",
	     'i',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_NONE,
	     &action_info,
	     /* TRANSLATORS: command line option */
	     _("Show the information of firmware update status"),
	     NULL},
	    {"enable",
	     'e',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_NONE,
	     &action_enable,
	     /* TRANSLATORS: command line option */
	     _("Enable firmware update support on supported systems"),
	     NULL},
	    {"esp-path",
	     'p',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_STRING,
	     &esp_path,
	     /* TRANSLATORS: command line option */
	     _("Override the default ESP path"),
	     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
	     _("PATH")},
	    {"set-debug",
	     'd',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_NONE,
	     &action_set_debug,
	     /* TRANSLATORS: command line option */
	     _("Set the debugging flag during update"),
	     NULL},
	    {"unset-debug",
	     'D',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_NONE,
	     &action_unset_debug,
	     /* TRANSLATORS: command line option */
	     _("Unset the debugging flag during update"),
	     NULL},
	    {"apply",
	     'a',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_STRING,
	     &apply,
	     /* TRANSLATORS: command line option */
	     _("Apply firmware updates"),
	     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
	     C_("A single GUID", "GUID")},
	    {"method",
	     'm',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_STRING,
	     &type,
	     /* TRANSLATORS: command line option */
	     _("Device update method"),
	     "nvram|cod|grub"},
	    {"flags",
	     'f',
	     G_OPTION_FLAG_NONE,
	     G_OPTION_ARG_STRING,
	     &flags,
	     /* TRANSLATORS: command line option */
	     _("Use quirk flags when installing firmware"),
	     NULL},
	    {NULL}};

	setlocale(LC_ALL, "");

	bindtextdomain(GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	/* ensure root user */
#ifdef HAVE_GETUID
	if (getuid() != 0 || geteuid() != 0)
		/* TRANSLATORS: we're poking around as a power user */
		g_printerr("%s\n", _("This program may only work correctly as root"));
#endif

	/* get a action_list of the commands */
	priv->context = g_option_context_new(NULL);
	g_option_context_set_description(
	    priv->context,
	    /* TRANSLATORS: CLI description */
	    _("This tool allows an administrator to debug UpdateCapsule operation."));

	/* TRANSLATORS: program name */
	g_set_application_name(_("UEFI Firmware Utility"));
	g_option_context_add_main_entries(priv->context, options, NULL);
	ret = g_option_context_parse(priv->context, &argc, &argv, &error);
	if (!ret) {
		/* TRANSLATORS: the user didn't read the man page */
		g_print("%s: %s\n", _("Failed to parse arguments"), error->message);
		return EXIT_FAILURE;
	}

	/* set verbose? */
	if (verbose) {
		g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
	} else {
		g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, fu_util_ignore_cb, NULL);
	}

	/* nothing specified */
	if (!action_enable && !action_info && !action_list && !action_log && !action_set_debug &&
	    !action_supported && !action_unset_debug && !action_version && apply == NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = g_option_context_get_help(priv->context, TRUE, NULL);
		g_printerr("%s\n\n%s", _("No action specified!"), tmp);
		return EXIT_FAILURE;
	}

	/* action_version first */
	if (action_version)
		g_print("fwupd version: %s\n", PACKAGE_VERSION);

	/* override the default ESP path */
	if (esp_path != NULL) {
		esp = fu_common_get_esp_for_path(esp_path, &error);
		if (esp == NULL) {
			/* TRANSLATORS: ESP is EFI System Partition */
			g_print("%s: %s\n", _("ESP specified was not valid"), error->message);
			return EXIT_FAILURE;
		}
	} else {
		esp = fu_common_get_esp_default(&error);
		if (esp == NULL) {
			g_printerr("failed: %s\n", error->message);
			return EXIT_FAILURE;
		}
	}

	/* show the debug action_log from the last attempted update */
	if (action_log) {
		gsize sz = 0;
		g_autofree guint8 *buf = NULL;
		g_autofree guint16 *buf_ucs2 = NULL;
		g_autofree gchar *str = NULL;
		g_autoptr(GError) error_local = NULL;
		if (!fu_efivar_get_data(FU_EFIVAR_GUID_FWUPDATE,
					"FWUPDATE_DEBUG_LOG",
					&buf,
					&sz,
					NULL,
					&error_local)) {
			g_printerr("failed: %s\n", error_local->message);
			return EXIT_FAILURE;
		}
		buf_ucs2 = g_new0(guint16, (sz / 2) + 1);
		memcpy(buf_ucs2, buf, sz);
		str = fu_ucs2_to_uft8(buf_ucs2, sz / 2);
		g_print("%s", str);
	}

	if (action_list || action_supported || action_info) {
		g_autoptr(FuContext) ctx = fu_context_new();
		g_autoptr(FuBackend) backend = fu_uefi_backend_new(ctx);
		g_autoptr(GError) error_local = NULL;

		/* add each device */
		if (!fu_backend_coldplug(backend, &error_local)) {
			g_printerr("failed: %s\n", error_local->message);
			return EXIT_FAILURE;
		}

		/* set ESP */
		devices = fu_backend_get_devices(backend);
		for (guint i = 0; i < devices->len; i++) {
			FuUefiDevice *dev = g_ptr_array_index(devices, i);
			fu_uefi_device_set_esp(dev, esp);
		}
	}

	/* action_list action_supported firmware updates */
	if (action_list) {
		for (guint i = 0; i < devices->len; i++) {
			FuUefiDevice *dev = g_ptr_array_index(devices, i);
			g_print("%s type, {%s} version %" G_GUINT32_FORMAT " can be updated "
				"to any version above %" G_GUINT32_FORMAT "\n",
				fu_uefi_device_kind_to_string(fu_uefi_device_get_kind(dev)),
				fu_uefi_device_get_guid(dev),
				fu_uefi_device_get_version(dev),
				fu_uefi_device_get_version_lowest(dev) - 1);
		}
	}

	/* query for firmware update support */
	if (action_supported) {
		if (devices->len > 0) {
			g_print("%s\n", _("Firmware updates are supported on this machine."));
		} else {
			g_print("%s\n", _("Firmware updates are not supported on this machine."));
		}
	}

	/* show the information of firmware update status */
	if (action_info) {
		for (guint i = 0; i < devices->len; i++) {
			FuUefiDevice *dev = g_ptr_array_index(devices, i);
			g_autoptr(FuUefiUpdateInfo) info = NULL;
			g_autoptr(GError) error_local = NULL;

			/* load any existing update info */
			info = fu_uefi_device_load_update_info(dev, &error_local);
			g_print("Information for the update status entry %u:\n", i);
			if (info == NULL) {
				if (g_error_matches(error_local,
						    G_IO_ERROR,
						    G_IO_ERROR_NOT_FOUND)) {
					g_print("  Firmware GUID: {%s}\n",
						fu_uefi_device_get_guid(dev));
					g_print("  Update Status: No update info found\n\n");
				} else {
					g_printerr("Failed: %s\n\n", error_local->message);
				}
				continue;
			}
			g_print("  Information Version: %" G_GUINT32_FORMAT "\n",
				fu_uefi_update_info_get_version(info));
			g_print("  Firmware GUID: {%s}\n", fu_uefi_update_info_get_guid(info));
			g_print("  Capsule Flags: 0x%08" G_GUINT32_FORMAT "x\n",
				fu_uefi_update_info_get_capsule_flags(info));
			g_print("  Hardware Instance: %" G_GUINT64_FORMAT "\n",
				fu_uefi_update_info_get_hw_inst(info));
			g_print("  Update Status: %s\n",
				fu_uefi_update_info_status_to_string(
				    fu_uefi_update_info_get_status(info)));
			g_print("  Capsule File Path: %s\n\n",
				fu_uefi_update_info_get_capsule_fn(info));
		}
	}

	/* action_enable firmware update support on action_supported systems */
	if (action_enable) {
		g_printerr("Unsupported, use `fwupdmgr unlock`\n");
		return EXIT_FAILURE;
	}

	/* set the debugging flag during update */
	if (action_set_debug) {
		const guint8 data = 1;
		g_autoptr(GError) error_local = NULL;
		if (!fu_efivar_set_data(FU_EFIVAR_GUID_FWUPDATE,
					"FWUPDATE_VERBOSE",
					&data,
					sizeof(data),
					FU_EFIVAR_ATTR_NON_VOLATILE |
					    FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
					    FU_EFIVAR_ATTR_RUNTIME_ACCESS,
					&error_local)) {
			g_printerr("failed: %s\n", error_local->message);
			return EXIT_FAILURE;
		}
		g_print("%s\n", _("Enabled fwupdate debugging"));
	}

	/* unset the debugging flag during update */
	if (action_unset_debug) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_efivar_delete(FU_EFIVAR_GUID_FWUPDATE, "FWUPDATE_VERBOSE", &error_local)) {
			g_printerr("failed: %s\n", error_local->message);
			return EXIT_FAILURE;
		}
		g_print("%s\n", _("Disabled fwupdate debugging"));
	}

	/* apply firmware updates */
	if (apply != NULL) {
		g_autoptr(FuContext) ctx = fu_context_new();
		g_autoptr(FuBackend) backend = fu_uefi_backend_new(ctx);
		g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
		g_autoptr(FuUefiDevice) dev = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GBytes) fw = NULL;

		/* type is specified, otherwise use default */
		if (type != NULL) {
			if (g_strcmp0(type, "nvram") == 0) {
				fu_uefi_backend_set_device_gtype(FU_UEFI_BACKEND(backend),
								 FU_TYPE_UEFI_NVRAM_DEVICE);
			} else if (g_strcmp0(type, "grub") == 0) {
				fu_uefi_backend_set_device_gtype(FU_UEFI_BACKEND(backend),
								 FU_TYPE_UEFI_GRUB_DEVICE);
			} else if (g_strcmp0(type, "cod") == 0) {
				fu_uefi_backend_set_device_gtype(FU_UEFI_BACKEND(backend),
								 FU_TYPE_UEFI_COD_DEVICE);
			} else {
				g_printerr("invalid type specified\n");
				return EXIT_FAILURE;
			}
		}

		if (argv[1] == NULL) {
			g_printerr("capsule filename required\n");
			return EXIT_FAILURE;
		}
		fw = fu_common_get_contents_bytes(argv[1], &error_local);
		if (fw == NULL) {
			g_printerr("failed: %s\n", error_local->message);
			return EXIT_FAILURE;
		}
		dev = fu_uefi_backend_device_new_from_guid(FU_UEFI_BACKEND(backend), apply);
		fu_uefi_device_set_esp(dev, esp);
		if (flags != NULL)
			fu_device_set_custom_flags(FU_DEVICE(dev), flags);
		if (!fu_device_prepare(FU_DEVICE(dev), FWUPD_INSTALL_FLAG_NONE, &error_local)) {
			g_printerr("failed: %s\n", error_local->message);
			return EXIT_FAILURE;
		}
		if (!fu_device_write_firmware(FU_DEVICE(dev),
					      fw,
					      progress,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error_local)) {
			g_printerr("failed: %s\n", error_local->message);
			return EXIT_FAILURE;
		}
		if (!fu_device_cleanup(FU_DEVICE(dev), FWUPD_INSTALL_FLAG_NONE, &error_local)) {
			g_printerr("failed: %s\n", error_local->message);
			return EXIT_FAILURE;
		}
	}

	/* success */
	return EXIT_SUCCESS;
}
