/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>

#define FU_TYPE_KINETIC_MST_CONNECTION (fu_kinetic_mst_connection_get_type ())
G_DECLARE_FINAL_TYPE (FuKineticMstConnection, fu_kinetic_mst_connection, FU, KINETIC_MST_CONNECTION, GObject)

FuKineticMstConnection	*fu_kinetic_mst_connection_new(gint fd);

gboolean	 fu_kinetic_mst_connection_read		(FuKineticMstConnection *self,
								 guint32	 offset,
								 guint8		*buf,
								 guint32	 length,
								 GError		**error);

gboolean	 fu_kinetic_mst_connection_write 		(FuKineticMstConnection *self,
								 guint32	 offset,
								 const guint8 	*buf,
								 guint32	 length,
								 GError		**error);

