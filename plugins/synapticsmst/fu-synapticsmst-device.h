/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#include "fu-synapticsmst-common.h"

G_BEGIN_DECLS

#define FU_SYNAPTICSMST_TYPE_DEVICE (fu_synapticsmst_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapticsmstDevice, fu_synapticsmst_device, FU, SYNAPTICSMST_DEVICE, GObject)

#define SYSFS_DRM_DP_AUX "/sys/class/drm_dp_aux_dev"

FuSynapticsmstDevice	*fu_synapticsmst_device_new	(FuSynapticsmstMode kind,
							 const gchar *aux_node,
							 guint8 layer,
							 guint16 rad);

/* helpers */
GPtrArray 	*fu_synapticsmst_device_get_guids 		(FuSynapticsmstDevice *self);
gboolean	 fu_synapticsmst_device_scan_cascade_device 	(FuSynapticsmstDevice *self,
								 GError **error,
								 guint8 tx_port);
gboolean	 fu_synapticsmst_device_open 			(FuSynapticsmstDevice *self,
								 GError	**error);

/* getters */
FuSynapticsmstMode fu_synapticsmst_device_get_kind	(FuSynapticsmstDevice *self);
guint16		 fu_synapticsmst_device_get_board_id	 	(FuSynapticsmstDevice *self);
const gchar	*fu_synapticsmst_device_get_version		(FuSynapticsmstDevice *self);
const gchar 	*fu_synapticsmst_device_get_chip_id_str 	(FuSynapticsmstDevice *self);
const gchar 	*fu_synapticsmst_device_get_aux_node		(FuSynapticsmstDevice *self);
guint16 	 fu_synapticsmst_device_get_rad 		(FuSynapticsmstDevice *self);
guint8 		 fu_synapticsmst_device_get_layer 		(FuSynapticsmstDevice *self);
gboolean	 fu_synapticsmst_device_get_cascade		(FuSynapticsmstDevice *self);

/* object methods */
gboolean	 fu_synapticsmst_device_enumerate_device 	(FuSynapticsmstDevice *devices,
								 GError **error);
gboolean	 fu_synapticsmst_device_write_firmware		(FuSynapticsmstDevice *self,
								 GBytes	*fw,
								 GFileProgressCallback	progress_cb,
								 gpointer user_data,
								 gboolean reboot,
								 gboolean install_force,
								 GError	**error);

G_END_DECLS
