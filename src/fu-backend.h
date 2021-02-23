/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-device.h"

#define FU_TYPE_BACKEND (fu_backend_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuBackend, fu_backend, FU, BACKEND, GObject)

struct _FuBackendClass
{
	GObjectClass		 parent_class;
	gboolean		 (*setup)		(FuBackend	*self,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
	gboolean		 (*coldplug)		(FuBackend	*self,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
	gboolean		 (*recoldplug)		(FuBackend	*self,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
};

const gchar	*fu_backend_get_name			(FuBackend	*self);
gboolean	 fu_backend_get_enabled			(FuBackend	*self);
void		 fu_backend_set_enabled			(FuBackend	*self,
							 gboolean	 enabled);
gboolean	 fu_backend_setup			(FuBackend	*self,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_backend_coldplug			(FuBackend	*self,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_backend_recoldplug			(FuBackend	*self,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
void		 fu_backend_device_added		(FuBackend	*self,
							 FuDevice	*device);
void		 fu_backend_device_removed		(FuBackend	*self,
							 FuDevice	*device);
void		 fu_backend_device_changed		(FuBackend	*self,
							 FuDevice	*device);
