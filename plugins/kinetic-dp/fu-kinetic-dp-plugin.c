/*
 * Copyright (C) FIXMEFIXMEFIXMEFIXMEFIXME
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-kinetic-dp-device.h"
#include "fu-kinetic-dp-plugin.h"

struct _FuKineticDpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuKineticDpPlugin, fu_kinetic_dp_plugin, FU_TYPE_PLUGIN)

static void
fu_kinetic_dp_plugin_init(FuKineticDpPlugin *self)
{
}

static void
fu_kinetic_dp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
}

static void
fu_kinetic_dp_finalize(GObject *obj)
{
	FuKineticDpPlugin *self = FU_KINETIC_DP_PLUGIN(obj);
	G_OBJECT_CLASS(fu_kinetic_dp_plugin_parent_class)->finalize(obj);
}

static void
fu_kinetic_dp_plugin_class_init(FuKineticDpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructed = fu_kinetic_dp_plugin_constructed;
	object_class->finalize = fu_kinetic_dp_finalize;
	plugin_class->startup = fu_kinetic_dp_plugin_startup;
}
