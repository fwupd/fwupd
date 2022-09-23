/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-cpu-device.h"
#include "fu-cpu-plugin.h"

struct _FuCpuPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCpuPlugin, fu_cpu_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_cpu_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuCpuDevice) dev = fu_cpu_device_new(ctx);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 99, "probe");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "setup");

	if (!fu_device_probe(FU_DEVICE(dev), error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_device_setup(FU_DEVICE(dev), error))
		return FALSE;
	fu_progress_step_done(progress);

	fu_plugin_cache_add(plugin, "cpu", dev);
	fu_plugin_device_add(plugin, FU_DEVICE(dev));
	return TRUE;
}

static void
fu_cpu_plugin_init(FuCpuPlugin *self)
{
}

static void
fu_cpu_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_BEFORE, "msr");
}

static void
fu_cpu_plugin_class_init(FuCpuPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_cpu_plugin_constructed;
	plugin_class->coldplug = fu_cpu_plugin_coldplug;
}
