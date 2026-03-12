/*
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cpu-plugin.h"

struct _FuCpuPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCpuPlugin, fu_cpu_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_cpu_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuProcessorDevice) dev = fu_processor_device_new(ctx);
	if (!fu_device_setup(FU_DEVICE(dev), error))
		return FALSE;
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
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "ProcessorMitigationsRequired");
	fu_context_add_quirk_key(ctx, "ProcessorSinkcloseMicrocodeVersion");
	fu_context_add_quirk_key(ctx, "ProcessorKind");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_BEFORE, "msr");
}

static void
fu_cpu_plugin_class_init(FuCpuPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_cpu_plugin_constructed;
	plugin_class->coldplug = fu_cpu_plugin_coldplug;
}
