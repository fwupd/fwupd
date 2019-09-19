/*
 * Copyright (C) 2019 Dell, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <errno.h>
#include <gpiod.h>

#include "fu-plugin-vfuncs.h"

typedef struct gpiod_chip GpioChip;
static void gpiod_chip_autoptr_cleanup (GpioChip *chip)
{
	gpiod_chip_close (chip);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GpioChip, gpiod_chip_autoptr_cleanup)

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "wacom-raw");
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	if (!fu_plugin_has_custom_flag (plugin, "supports-wacom-recovery")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return FALSE;
	}
	return TRUE;
}

static gint
fu_plugin_wacom_recovery_read_gpio_value (const gchar *name, guint i)
{
	g_autoptr(GpioChip) gpio_chip = NULL;
	struct gpiod_line *gpio_line;
	gint ret;
	gpio_chip = gpiod_chip_open_by_name (name);
	if (gpio_chip == NULL) {
		g_debug ("couldn't find %s", name);
		return FALSE;
	}
	gpio_line = gpiod_chip_get_line (gpio_chip, i);
	if (gpio_line == NULL) {
		g_debug ("couldn't get %s line %u", name, i);
		return FALSE;
	}
	ret = gpiod_line_get_value (gpio_line);
	if (ret < 0) {
		g_debug ("failed to read line %u: %s", i, strerror (errno));
		return FALSE;
	}
	return ret;
}

static const gchar *
fu_plugin_wacom_recovery_lookup_guid (FuPlugin *plugin)
{
	GPtrArray *hwids = fu_plugin_get_hwids (plugin);
	FuQuirks *quirks = fu_plugin_get_quirks (plugin);

	/* Try to match all system HWIDs */
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *hwid = g_ptr_array_index (hwids, i);
		g_autofree gchar *key = g_strdup_printf ("HwId=%s", hwid);
		g_auto(GStrv) gpios = NULL;
		const gchar *lines;
		const gchar *chip;

		/* Look for a quirk that has WacomRecoveryGpioChip */
		chip = fu_quirks_lookup_by_id (quirks, key, "WacomRecoveryGpioChip");
		if (chip == NULL)
			continue;

		/* Look for a quirk that has WacomRecoveryGpioLines */
		lines = fu_quirks_lookup_by_id (quirks, key, "WacomRecoveryGpioLines");
		if (lines == NULL)
			continue;

		/* Check the value of each of these GPIOs */
		gpios = g_strsplit (lines, ",", -1);
		for (guint j = 0; gpios[j] != NULL; j++) {
			g_autofree gchar *gpio_id = NULL;
			gint64 line = fu_common_strtoull (gpios[j]);

			if (!fu_plugin_wacom_recovery_read_gpio_value (chip,
								       line)) {
				g_debug ("%s line %" G_GINT64_FORMAT "is low", chip, line);
				continue;
			}
			gpio_id = g_strdup_printf ("WacomRecoveryGpio%" G_GINT64_FORMAT, line);
			return fu_quirks_lookup_by_id (quirks, key, gpio_id);
		}
	}
	return NULL;
}


void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	const gchar *guid;
	/* only receive devices that are in bootloader mode from wacom-raw */
	if (g_strcmp0 (fu_device_get_plugin (device), "wacom-raw") != 0)
		return;
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("%s not bootloader", fu_device_get_name (device));
		return;
	}

	/* lookup the GUID to insert based upon GPIO */
	guid = fu_plugin_wacom_recovery_lookup_guid (plugin);
	if (guid != NULL) {
		fu_device_add_guid (device, guid);
		/* TODO: decide if needed */
		//fu_device_set_version (device, "0");
	}
}
