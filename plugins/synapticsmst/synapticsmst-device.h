/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define SYNAPTICSMST_TYPE_DEVICE (synapticsmst_device_get_type ())
G_DECLARE_FINAL_TYPE (SynapticsMSTDevice, synapticsmst_device, SYNAPTICSMST, DEVICE, GObject)

#define SYSFS_DRM_DP_AUX "/sys/class/drm_dp_aux_dev"

/**
 * SynapticsMSTDeviceKind:
 * @SYNAPTICSMST_DEVICE_KIND_UNKNOWN:		Type invalid or not known
 * @SYNAPTICSMST_DEVICE_KIND_DIRECT:		Directly addressable
 * @SYNAPTICSMST_DEVICE_KIND_REMOTE:		Requires remote register work
 *
 * The device type.
 **/
typedef enum {
	SYNAPTICSMST_DEVICE_KIND_UNKNOWN,
	SYNAPTICSMST_DEVICE_KIND_DIRECT,
	SYNAPTICSMST_DEVICE_KIND_REMOTE,
	/*< private >*/
	SYNAPTICSMST_DEVICE_KIND_LAST
} SynapticsMSTDeviceKind;

typedef enum {
	SYNAPTICSMST_CHIP_KIND_UNKNOWN,
	SYNAPTICSMST_CHIP_KIND_TESLA_LEAF,
	SYNAPTICSMST_CHIP_KIND_PANAMERA,
	/*<private >*/
	SYNAPTICSMST_CHIP_KIND_LAST
} SynapticsMSTChipKind;

#define CUSTOMERID_DELL 	0x1

SynapticsMSTDevice	*synapticsmst_device_new	(SynapticsMSTDeviceKind kind,
							 const gchar *aux_node,
							 guint8 layer,
							 guint16 rad);

/* helpers */
SynapticsMSTDeviceKind synapticsmst_device_kind_from_string	(const gchar	*kind);
const gchar	*synapticsmst_device_kind_to_string		(SynapticsMSTDeviceKind kind);
GPtrArray 	*synapticsmst_device_get_guids 			(SynapticsMSTDevice *self);
gboolean	 synapticsmst_device_scan_cascade_device 	(SynapticsMSTDevice *self,
								 GError **error,
								 guint8 tx_port);
gboolean	 synapticsmst_device_open 			(SynapticsMSTDevice *self,
								 GError	**error);

/* getters */
SynapticsMSTDeviceKind synapticsmst_device_get_kind		(SynapticsMSTDevice *self);
guint16		 synapticsmst_device_get_board_id	 	(SynapticsMSTDevice *self);
const gchar	*synapticsmst_device_get_version		(SynapticsMSTDevice *self);
const gchar 	*synapticsmst_device_get_chip_id_str 		(SynapticsMSTDevice *self);
const gchar 	*synapticsmst_device_get_aux_node		(SynapticsMSTDevice *self);
guint16 	 synapticsmst_device_get_rad 			(SynapticsMSTDevice *self);
guint8 		 synapticsmst_device_get_layer 			(SynapticsMSTDevice *self);
gboolean	 synapticsmst_device_get_cascade		(SynapticsMSTDevice *self);

/* object methods */
gboolean	 synapticsmst_device_enumerate_device 		(SynapticsMSTDevice *devices,
								 GError **error);
gboolean	 synapticsmst_device_write_firmware		(SynapticsMSTDevice *self,
								 GBytes	*fw,
								 GFileProgressCallback	progress_cb,
								 gpointer user_data,
								 gboolean reboot,
								 gboolean install_force,
								 GError	**error);

G_END_DECLS
