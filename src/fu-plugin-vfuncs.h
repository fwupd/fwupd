/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_PLUGIN_VFUNCS_H
#define __FU_PLUGIN_VFUNCS_H

#include "fu-plugin.h"
#include "fu-device.h"

G_BEGIN_DECLS

void		 fu_plugin_init				(FuPlugin	*plugin);
void		 fu_plugin_destroy			(FuPlugin	*plugin);
gboolean	 fu_plugin_startup			(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_coldplug			(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_coldplug_prepare		(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_coldplug_cleanup		(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_recoldplug			(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_update			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GBytes		*blob_fw,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_plugin_verify			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 FuPluginVerifyFlags flags,
							 GError		**error);
gboolean	 fu_plugin_unlock			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_clear_results		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_get_results			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_update_attach		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_update_detach		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_update_reload		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_update_prepare		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_update_cleanup		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_usb_device_added		(FuPlugin	*plugin,
							 GUsbDevice	*usb_device,
							 GError		**error);
void		 fu_plugin_device_registered		(FuPlugin	*plugin,
							 FuDevice	*dev);

G_END_DECLS

#endif /* __FU_PLUGIN_VFUNCS_H */
