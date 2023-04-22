/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WAC_MODULE (fu_wac_module_get_type())
G_DECLARE_DERIVABLE_TYPE(FuWacModule, fu_wac_module, FU, WAC_MODULE, FuDevice)

struct _FuWacModuleClass {
	FuDeviceClass parent_class;
};

#define FU_WAC_MODULE_WRITE_TIMEOUT  2000  /* ms */
#define FU_WAC_MODULE_ERASE_TIMEOUT  15000 /* ms */
#define FU_WAC_MODULE_FINISH_TIMEOUT 1000  /* ms */
#define FU_WAC_MODULE_COMMIT_TIMEOUT 80000 /* ms */

gboolean
fu_wac_module_set_feature(FuWacModule *self,
			  guint8 command,
			  GBytes *blob,
			  FuProgress *progress,
			  guint busy_timeout,
			  GError **error);
