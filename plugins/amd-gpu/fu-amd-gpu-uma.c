/*
 * Copyright 2026 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>
#include <string.h>

#include "fu-amd-gpu-uma.h"

typedef struct _FuAmdGpuUmaSetting {
	FwupdBiosSetting parent_instance;
	GHashTable *value_map;	       /* maps display value to index for writing */
	GHashTable *reverse_value_map; /* maps index to display value for reading */
	gchar *uma_path;
} FuAmdGpuUmaSetting;

#define UMA_CARVEOUT_OPTIONS_FILE "carveout_options"
#define UMA_CARVEOUT_FILE	  "carveout"
#define UMA_DIR			  "uma"

G_DEFINE_TYPE(FuAmdGpuUmaSetting, fu_amd_gpu_uma_setting, FWUPD_TYPE_BIOS_SETTING)

static gboolean
fu_amd_gpu_uma_setting_write_value(FwupdBiosSetting *self, const gchar *value, GError **error)
{
	FuAmdGpuUmaSetting *setting = FU_AMD_GPU_UMA_SETTING(self);
	g_autofree gchar *carveout_file = NULL;
	g_autoptr(FuIOChannel) io = NULL;
	const gchar *index_to_write = NULL;

	if (setting->uma_path == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "UMA path not set");
		return FALSE;
	}

	index_to_write = g_hash_table_lookup(setting->value_map, value);
	if (index_to_write == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Invalid value '%s'",
			    value);
		return FALSE;
	}

	carveout_file = g_build_filename(setting->uma_path, UMA_CARVEOUT_FILE, NULL);
	io = fu_io_channel_new_file(carveout_file, FU_IO_CHANNEL_OPEN_FLAG_WRITE, error);
	if (io == NULL)
		return FALSE;

	if (!fu_io_channel_write_raw(io,
				     (const guint8 *)index_to_write,
				     strlen(index_to_write),
				     1000,
				     FU_IO_CHANNEL_FLAG_NONE,
				     error)) {
		g_prefix_error(error, "failed to write to %s: ", carveout_file);
		return FALSE;
	}

	fwupd_bios_setting_set_current_value(self, value);
	g_debug("set %s to %s (index: %s)", fwupd_bios_setting_get_id(self), value, index_to_write);
	return TRUE;
}

static void
fu_amd_gpu_uma_setting_finalize(GObject *obj)
{
	FuAmdGpuUmaSetting *setting = FU_AMD_GPU_UMA_SETTING(obj);
	g_hash_table_unref(setting->value_map);
	g_hash_table_unref(setting->reverse_value_map);
	g_free(setting->uma_path);
	G_OBJECT_CLASS(fu_amd_gpu_uma_setting_parent_class)->finalize(obj);
}

static void
fu_amd_gpu_uma_setting_init(FuAmdGpuUmaSetting *self)
{
	FuAmdGpuUmaSetting *setting = FU_AMD_GPU_UMA_SETTING(self);
	setting->value_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	setting->reverse_value_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

static void
fu_amd_gpu_uma_setting_class_init(FuAmdGpuUmaSettingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FwupdBiosSettingClass *bios_setting_class = FWUPD_BIOS_SETTING_CLASS(klass);
	object_class->finalize = fu_amd_gpu_uma_setting_finalize;
	bios_setting_class->write_value = fu_amd_gpu_uma_setting_write_value;
}

/**
 * fu_amd_gpu_uma_read_file:
 * @path: full path to file to read
 * @error: a #GError or NULL
 *
 * Reads a file and returns its contents as a string (stripped of whitespace).
 *
 * Returns: (transfer full): file contents or NULL on error
 **/
static gchar *
fu_amd_gpu_uma_read_file(const gchar *path, GError **error)
{
	gchar *content = NULL;

	if (!g_file_get_contents(path, &content, NULL, error)) {
		g_prefix_error(error, "failed to read %s: ", path);
		return NULL;
	}

	g_strchomp(content);
	return content;
}

/**
 * fu_amd_gpu_uma_check_support:
 * @device_sysfs_path: the device sysfs path
 * @error: a #GError or NULL
 *
 * Checks if UMA carveout support is available on this device.
 *
 * Returns: TRUE if UMA carveout is supported, FALSE otherwise
 **/
gboolean
fu_amd_gpu_uma_check_support(const gchar *device_sysfs_path, GError **error)
{
	g_autofree gchar *uma_dir = NULL;
	g_autofree gchar *carveout_file = NULL;
	g_autofree gchar *options_file = NULL;

	g_return_val_if_fail(device_sysfs_path != NULL, FALSE);

	uma_dir = g_build_filename(device_sysfs_path, UMA_DIR, NULL);

	carveout_file = g_build_filename(uma_dir, UMA_CARVEOUT_FILE, NULL);
	options_file = g_build_filename(uma_dir, UMA_CARVEOUT_OPTIONS_FILE, NULL);

	if (!g_file_test(carveout_file, G_FILE_TEST_EXISTS) ||
	    !g_file_test(options_file, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "UMA carveout not supported on this device");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_amd_gpu_uma_get_setting:
 * @device_sysfs_path: the device sysfs path
 * @error: a #GError or NULL
 *
 * Creates a FwupdBiosSetting object for the UMA carveout configuration.
 * Reads the available options from carveout_options and current value from carveout.
 *
 * Returns: (transfer full): FwupdBiosSetting object or NULL on error
 **/
FwupdBiosSetting *
fu_amd_gpu_uma_get_setting(const gchar *device_sysfs_path, GError **error)
{
	FuAmdGpuUmaSetting *setting = NULL;
	const gchar *display_current = NULL;
	g_autofree gchar *uma_dir = NULL;
	g_autofree gchar *options_file = NULL;
	g_autofree gchar *carveout_file = NULL;
	g_autofree gchar *options_content = NULL;
	g_autofree gchar *current_value = NULL;
	g_autoptr(FuAmdGpuUmaSetting) attr = NULL;
	g_auto(GStrv) lines = NULL;

	g_return_val_if_fail(device_sysfs_path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_amd_gpu_uma_check_support(device_sysfs_path, error))
		return NULL;

	uma_dir = g_build_filename(device_sysfs_path, UMA_DIR, NULL);

	attr = g_object_new(FU_TYPE_AMD_GPU_UMA_SETTING, NULL);
	setting = FU_AMD_GPU_UMA_SETTING(attr);
	setting->uma_path = g_strdup(uma_dir);

	fwupd_bios_setting_set_name(FWUPD_BIOS_SETTING(attr), "Dedicated Video Memory");
	fwupd_bios_setting_set_id(FWUPD_BIOS_SETTING(attr), "com.amd-gpu.uma_carveout");
	fwupd_bios_setting_set_description(
	    FWUPD_BIOS_SETTING(attr),
	    "GPU unified memory architecture carveout size for system memory");
	fwupd_bios_setting_set_kind(FWUPD_BIOS_SETTING(attr), FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	fwupd_bios_setting_set_path(FWUPD_BIOS_SETTING(attr), uma_dir);

	options_file = g_build_filename(uma_dir, UMA_CARVEOUT_OPTIONS_FILE, NULL);
	options_content = fu_amd_gpu_uma_read_file(options_file, error);
	if (options_content == NULL)
		return NULL;

	lines = g_strsplit(options_content, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		gchar *line = lines[i];
		gchar *description = NULL;
		g_autofree gchar *index_str = NULL;
		g_autofree gchar *display_value = NULL;
		g_auto(GStrv) parts = g_strsplit(line, ":", 2);

		if (parts[0] == NULL || parts[1] == NULL)
			continue;

		index_str = g_strdup(parts[0]);
		description = g_strstrip(parts[1]);

		display_value = g_strdup(description);

		fwupd_bios_setting_add_possible_value(FWUPD_BIOS_SETTING(attr), display_value);

		g_hash_table_insert(setting->value_map,
				    g_strdup(display_value),
				    g_strdup(index_str));
		g_hash_table_insert(setting->reverse_value_map,
				    g_strdup(index_str),
				    g_strdup(display_value));
	}

	carveout_file = g_build_filename(uma_dir, UMA_CARVEOUT_FILE, NULL);
	current_value = fu_amd_gpu_uma_read_file(carveout_file, error);
	if (current_value == NULL)
		return NULL;

	display_current = g_hash_table_lookup(setting->reverse_value_map, current_value);
	if (display_current != NULL)
		fwupd_bios_setting_set_current_value(FWUPD_BIOS_SETTING(attr), display_current);

	fwupd_bios_setting_set_filename(FWUPD_BIOS_SETTING(attr), UMA_CARVEOUT_FILE);
	return FWUPD_BIOS_SETTING(g_steal_pointer(&attr));
}
