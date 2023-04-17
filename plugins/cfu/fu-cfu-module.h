/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CFU_MODULE (fu_cfu_module_get_type())
G_DECLARE_FINAL_TYPE(FuCfuModule, fu_cfu_module, FU, CFU_MODULE, FuDevice)

guint8
fu_cfu_module_get_component_id(FuCfuModule *self);
gboolean
fu_cfu_module_setup(FuCfuModule *self,
		    const guint8 *buf,
		    gsize bufsz,
		    gsize offset,
		    GError **error);
FuCfuModule *
fu_cfu_module_new(FuDevice *parent);
