/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-gpio-device.h"

struct FuPluginData {
	GPtrArray *current_logical_ids; /* element-type: utf-8 */
};

static void
fu_plugin_gpio_init(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuPluginData *data = fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
	data->current_logical_ids = g_ptr_array_new_with_free_func(g_free);
	fu_context_add_quirk_key(ctx, "GpioForUpdate");
	fu_plugin_add_udev_subsystem(plugin, "gpio");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GPIO_DEVICE);
}

static void
fu_plugin_gpio_destroy(FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_ptr_array_unref(data->current_logical_ids);
}

static gboolean
fu_plugin_gpio_parse_level(const gchar *str, gboolean *ret, GError **error)
{
	if (g_strcmp0(str, "high") == 0) {
		*ret = TRUE;
		return TRUE;
	}
	if (g_strcmp0(str, "low") == 0) {
		*ret = FALSE;
		return TRUE;
	}
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    "cannot parse level, got %s and expected high|low",
		    str);
	return FALSE;
}

static gboolean
fu_plugin_gpio_process_quirk(FuPlugin *self, const gchar *str, GError **error)
{
	FuPluginData *data = fu_plugin_get_data(self);
	gboolean value = FALSE;
	FuDevice *device_tmp;
	g_auto(GStrv) split = g_strsplit(str, ",", -1);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* sanity check */
	if (g_strv_length(split) != 3) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid format, CHIP_NAME,PIN_NAME,LEVEL, got '%s'",
			    str);
		return FALSE;
	}
	if (!fu_plugin_gpio_parse_level(split[2], &value, error))
		return FALSE;
	device_tmp = fu_plugin_cache_lookup(self, split[0]);
	if (device_tmp == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "GPIO device %s not found",
			    split[0]);
		return FALSE;
	}
	locker = fu_device_locker_new(device_tmp, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_gpio_device_assign(FU_GPIO_DEVICE(device_tmp), split[1], value, error)) {
		g_prefix_error(error, "failed to assign %s: ", split[0]);
		return FALSE;
	}

	/* success */
	g_ptr_array_add(data->current_logical_ids, g_strdup(fu_device_get_logical_id(device_tmp)));
	return TRUE;
}

static gboolean
fu_plugin_gpio_prepare(FuPlugin *self, FuDevice *device, FwupdInstallFlags flags, GError **error)
{
	GPtrArray *guids = fu_device_get_guids(device);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		const gchar *str;
		str = fu_context_lookup_quirk_by_id(fu_plugin_get_context(self),
						    guid,
						    "GpioForUpdate");
		if (str == NULL)
			continue;
		if (!fu_plugin_gpio_process_quirk(self, str, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_gpio_cleanup(FuPlugin *self, FuDevice *device, FwupdInstallFlags flags, GError **error)
{
	FuPluginData *data = fu_plugin_get_data(self);
	g_autoptr(GPtrArray) current_logical_ids = NULL;

	/* deep copy to local to clear transaction array */
	current_logical_ids =
	    g_ptr_array_copy(data->current_logical_ids, (GCopyFunc)g_strdup, NULL);
	g_ptr_array_set_size(data->current_logical_ids, 0);

	/* close the fds we opened during ->prepare */
	for (guint i = 0; i < current_logical_ids->len; i++) {
		FuDevice *device_tmp;
		const gchar *current_logical_id = g_ptr_array_index(current_logical_ids, i);

		device_tmp = fu_plugin_cache_lookup(self, current_logical_id);
		if (device_tmp == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "GPIO device %s no longer found",
				    current_logical_id);
			return FALSE;
		}
		if (!fu_gpio_device_unassign(FU_GPIO_DEVICE(device_tmp), error)) {
			g_prefix_error(error, "failed to unassign %s: ", current_logical_id);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_plugin_gpio_device_added(FuPlugin *self, FuDevice *device)
{
	fu_plugin_cache_add(self, fu_device_get_logical_id(device), device);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_gpio_init;
	vfuncs->destroy = fu_plugin_gpio_destroy;
	vfuncs->prepare = fu_plugin_gpio_prepare;
	vfuncs->cleanup = fu_plugin_gpio_cleanup;
	vfuncs->device_added = fu_plugin_gpio_device_added;
}
